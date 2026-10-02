#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidatePattern('^ocpp\.[a-z0-9]+([.-][a-z0-9]+)*$')]
    [ValidateLength(6,253)]
    [string]$Domain = 'ocpp.barryofeverything.com',
    [string]$KeyPath = '',
    [ValidateRange(1024,65535)][int]$Port = 5003,
    [ValidateRange(1,4)][int]$BuildJobs = 1,
    [switch]$CreateDns,
    [Security.SecureString]$DnsApiToken,
    [switch]$ApplyMigrations,
    [switch]$PrepareOnly,
    [switch]$PackageOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Domain = $Domain.ToLowerInvariant()
$server = '104.248.96.73'
$remote = "root@$server"
$hostKey = 'SHA256:7Zda6mpMXh05H2Pk8QEdHLdQ3comzT/3UdGSMcWoovg'
if (-not $KeyPath) { $KeyPath = Join-Path $PSScriptRoot '../material/nene_key_private.ppk' }

function Find-Tool([string]$name) {
    $command = Get-Command "$name.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path $env:ProgramFiles "PuTTY/$name.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    throw "Missing $name.exe. Install PuTTY (plink, pscp and Pageant), and Windows tar.exe."
}
function Run-Tool([string]$exe, [string[]]$arguments) {
    # Windows PowerShell treats native stderr warnings as errors under Stop.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $exe @arguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $saved }
    if ($code -ne 0) { throw "$([IO.Path]::GetFileName($exe)) failed (exit $code). See output above." }
}
function Ensure-Agent {
    if (Test-Agent) { return }
    $resolvedKey = (Resolve-Path -LiteralPath $KeyPath).ProviderPath
    $pageant = Find-Tool 'pageant'
    Write-Host 'Unlock the SSH key in the Pageant prompt. The deploy script does not save the passphrase.'
    Start-Process -FilePath $pageant -ArgumentList @(('"'+$resolvedKey+'"')) -WindowStyle Hidden | Out-Null
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        Start-Sleep -Seconds 2
        if (Test-Agent) { return }
    }
    throw 'SSH key was not unlocked in Pageant, or the pinned host could not be reached.'
}
function Test-Agent {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $script:plink -batch -agent -hostkey $hostKey $remote true 1>$null 2>$null
        return ($LASTEXITCODE -eq 0)
    } finally { $ErrorActionPreference = $saved }
}
function Ensure-Dns {
    $zone = $Domain.Substring(5)
    if (-not $DnsApiToken) { $script:DnsApiToken = Read-Host 'DigitalOcean API token with DNS read/create access' -AsSecureString }
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($script:DnsApiToken)
    try {
        $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
        $headers = @{Authorization="Bearer $token"}
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $url = "https://api.digitalocean.com/v2/domains/$zone/records?per_page=200&name=$Domain"
        $answer = Invoke-RestMethod -Method Get -Uri $url -Headers $headers
        $records = @($answer.domain_records | Where-Object {$_.name -in @('ocpp',$Domain) -and $_.type -in 'A','AAAA','CNAME'})
        if ($records.Count -gt 0) {
            if (@($records | Where-Object {$_.type -ne 'A' -or $_.data -ne $server}).Count -gt 0) {
                throw 'Existing ocpp DNS records conflict with this server. They were not overwritten.'
            }
            Write-Host 'DNS A record already points to the deployment server.'
        } else {
            $body = @{type='A';name='ocpp';data=$server;ttl=300} | ConvertTo-Json -Compress
            $null = Invoke-RestMethod -Method Post -Uri "https://api.digitalocean.com/v2/domains/$zone/records" -Headers $headers -ContentType 'application/json' -Body $body
            Write-Host "Created DNS A $Domain -> $server"
        }
    } catch {
        # REST errors may include request details; keep the token out of diagnostics.
        throw 'DNS creation/check failed. Verify DigitalOcean DNS token permissions and the existing ocpp records.'
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
        $token = $null; $headers = $null
    }
}

$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$releaseId = "$stamp-$([Guid]::NewGuid().ToString('N').Substring(0,12))"
$stage = Join-Path $PSScriptRoot ".deps/deploy/$releaseId"
$null = New-Item -ItemType Directory -Path $stage -Force
$archive = Join-Path $stage 'source.tar.gz'
$tar = Find-Tool 'tar'
$entries = @('CMakeLists.txt','cmake','include','src','ui','schemas','migrations','patches','deploy','scripts','tests','README.md','CONTRIBUTING.md','.clang-format')
foreach ($entry in $entries) {
    $path = Join-Path $PSScriptRoot $entry
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing release source: $entry" }
    if (@(Get-ChildItem -LiteralPath $path -Recurse -Force -ErrorAction Stop | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -gt 0) {
        throw "Release source contains a symbolic link: $entry"
    }
}
Write-Host 'Packaging source, schemas, UI and deployment templates (private config is excluded)...'
Run-Tool $tar (@('-czf',$archive,'--exclude=__pycache__','--exclude=*.pyc','--exclude=config.local.json',
    '--exclude=*.ppk','--exclude=*.pem','--exclude=*.key','--exclude=*.env','--exclude=.git','-C',$PSScriptRoot) + $entries)
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $stage 'source.sha256'), "$hash`n", (New-Object Text.UTF8Encoding($false)))
if ($PackageOnly) { Write-Host "Package ready: $archive"; return }

$script:plink = Find-Tool 'plink'
$pscp = Find-Tool 'pscp'
Ensure-Agent
if ($CreateDns) { Ensure-Dns }
if (-not $PrepareOnly) {
    $resolved = @()
    try { $resolved = @([Net.Dns]::GetHostAddresses($Domain) | ForEach-Object {$_.IPAddressToString}) } catch { }
    if ($resolved.Count -eq 0 -or @($resolved | Where-Object {$_ -ne $server}).Count -gt 0) {
        throw "DNS is not ready. Create A record ocpp -> $server in $($Domain.Substring(5)), or use -CreateDns. Use -PrepareOnly to build/install the local service while waiting for DNS."
    }
}
$remoteStage = "/var/tmp/ocpp-deploy-$releaseId"
$common = @('-batch','-agent','-hostkey',$hostKey)
Run-Tool $script:plink ($common + @($remote,"umask 077; mkdir -m 700 '$remoteStage'"))
Run-Tool $pscp ($common + @($archive,"${remote}:$remoteStage/source.tar.gz"))
# Convert the launch script to LF before uploading, regardless of Git checkout settings.
$launcher = Join-Path $stage 'remote-deploy.sh'
$shell = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'deploy/remote-deploy.sh')).Replace("`r`n","`n")
[IO.File]::WriteAllText($launcher,$shell,(New-Object Text.UTF8Encoding($false)))
Run-Tool $pscp ($common + @($launcher,"${remote}:$remoteStage/remote-deploy.sh"))
$mode = if ($PrepareOnly) {'prepare'} else {'deploy'}
$migrate = if ($ApplyMigrations) {'1'} else {'0'}
Write-Host "Building Linux release and configuring systemd/Nginx for $Domain..."
Run-Tool $script:plink ($common + @($remote,"bash '$remoteStage/remote-deploy.sh' '$releaseId' '$Domain' '$mode' '$Port' '$BuildJobs' '$migrate' '$hash'"))
if ($PrepareOnly) {
    Write-Host "Prepared server. Create DNS A $Domain -> $server and rerun ./deploy.ps1." -ForegroundColor Green
} else {
    $page = Invoke-WebRequest -UseBasicParsing -Uri "https://$Domain/health/ready" -TimeoutSec 20
    if ($page.StatusCode -ne 200) { throw 'Public HTTPS readiness check failed.' }
    Write-Host "Deployed: https://$Domain/" -ForegroundColor Green
    Write-Host 'Use the API token already in config.local.json to sign in. Charger URL: wss://<domain>/ocpp/<station-id>'
}
