#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidatePattern('^[a-z0-9]+([.-][a-z0-9]+)*\.[a-z]{2,}$')]
    [ValidateLength(4,180)][string]$BaseDomain = 'barryofeverything.com',
    [string]$KeyPath = '',
    [ValidateRange(1,4)][int]$BuildJobs = 1,
    [switch]$PrepareOnly,
    [switch]$PackageOnly,
    [switch]$SkipFrontendBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$server = '104.248.96.73'
$remote = "root@$server"
$hostKey = 'SHA256:7Zda6mpMXh05H2Pk8QEdHLdQ3comzT/3UdGSMcWoovg'
$domains = @('ev.admin','ev.admin.api','ev.customer','ev.customer.api') | ForEach-Object { "$_.$BaseDomain" }
if (-not $KeyPath) { $KeyPath = Join-Path $PSScriptRoot '../material/nene_key_private.ppk' }
function Find-Tool([string]$name) {
    $command = Get-Command "$name.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $path = Join-Path $env:ProgramFiles "PuTTY/$name.exe"
    if (Test-Path -LiteralPath $path) { return $path }
    throw "Missing $name.exe. Install PuTTY and Windows tar.exe."
}
function Run-Tool([string]$tool, [string[]]$arguments) {
    $saved = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & $tool @arguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $saved }
    if ($code) { throw "$([IO.Path]::GetFileName($tool)) failed (exit $code)." }
}
if (-not $PrepareOnly -and -not $PackageOnly) {
    foreach ($domain in $domains) {
        $addresses = @()
        try { $addresses = @([Net.Dns]::GetHostAddresses($domain) | ForEach-Object { $_.IPAddressToString }) } catch { }
        if ($addresses.Count -eq 0 -or @($addresses | Where-Object { $_ -ne $server }).Count) {
            throw "DNS not ready: create A $($domain.Substring(0, $domain.Length - $BaseDomain.Length - 1)) -> $server in $BaseDomain. Use -PrepareOnly while waiting."
        }
    }
}
if (-not $SkipFrontendBuild) { & (Join-Path $PSScriptRoot 'scripts/build-frontend.ps1') }
$id = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,12)
$stage = Join-Path $PSScriptRoot ".deps/deploy/$id"
$null = New-Item -ItemType Directory -Path $stage -Force
$entries = @('CMakeLists.txt','cmake','shared/domain','shared/transport','shared/frontend/wasm',
    'management/backend','customer/backend','tests','deploy','README.md','CONTRIBUTING.md')
foreach ($entry in $entries) {
    $path = Join-Path $PSScriptRoot $entry
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing source: $entry" }
    if (@(Get-ChildItem -LiteralPath $path -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw "Source contains a symbolic link: $entry"
    }
}
$payload = Join-Path $stage 'payload'
$null = New-Item -ItemType Directory -Path $payload
foreach ($entry in $entries) {
    $destination = Join-Path $payload $entry
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry) -Destination $destination -Recurse
}
foreach ($area in @('management','customer')) {
    $dist = Join-Path $PSScriptRoot "$area/frontend/dist"
    if (-not (Test-Path -LiteralPath "$dist/index.html") -or -not (Test-Path -LiteralPath "$dist/wasm/billing.wasm")) {
        throw "Production frontend missing: $area. Run scripts/build-frontend.ps1."
    }
    if (@(Get-ChildItem -LiteralPath $dist -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw 'Frontend output must not contain symbolic links.'
    }
    $web = Join-Path $payload 'web'
    $null = New-Item -ItemType Directory -Path $web -Force
    Copy-Item -LiteralPath $dist -Destination (Join-Path $web $area) -Recurse
}
$archive = Join-Path $stage 'source.tar.gz'
Run-Tool (Find-Tool 'tar') @('-czf',$archive,'--exclude=__pycache__','--exclude=*.pyc',
    '--exclude=config.local.json','--exclude=*.ppk','--exclude=*.pem','--exclude=*.key',
    '--exclude=.env','--exclude=*.env','--exclude=.git','--exclude=node_modules','-C',$payload,'.')
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($PackageOnly) { Write-Host "Billing package ready: $archive"; return }
$plink = Find-Tool 'plink'; $pscp = Find-Tool 'pscp'
$common = @('-batch','-agent','-hostkey',$hostKey)
function Test-Agent {
    $saved = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & $plink @common $remote true 1>$null 2>$null; return ($LASTEXITCODE -eq 0) }
    finally { $ErrorActionPreference = $saved }
}
if (-not (Test-Agent)) {
    $key = (Resolve-Path -LiteralPath $KeyPath).ProviderPath
    Start-Process -FilePath (Find-Tool 'pageant') -ArgumentList ('"' + $key + '"') -WindowStyle Hidden | Out-Null
    Write-Host 'Unlock the SSH key in Pageant. No passphrase is stored by this script.'
    for ($attempt=0; $attempt -lt 60; $attempt++) { Start-Sleep -Seconds 2; if (Test-Agent) { break } }
    if (-not (Test-Agent)) { throw 'SSH key is not unlocked or the pinned server cannot be reached.' }
}
$remoteStage = "/var/tmp/billing-deploy-$id"
Run-Tool $plink ($common + @($remote,"umask 077; mkdir -m 700 '$remoteStage'"))
Run-Tool $pscp ($common + @($archive,"${remote}:$remoteStage/source.tar.gz"))
$launcher = Join-Path $stage 'remote-deploy.sh'
$shell = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'deploy/remote-deploy.sh')).Replace("`r`n","`n")
[IO.File]::WriteAllText($launcher,$shell,(New-Object Text.UTF8Encoding($false)))
Run-Tool $pscp ($common + @($launcher,"${remote}:$remoteStage/remote-deploy.sh"))
$mode = if ($PrepareOnly) { 'prepare' } else { 'deploy' }
Run-Tool $plink ($common + @($remote,"bash '$remoteStage/remote-deploy.sh' '$id' '$BaseDomain' '$mode' '$BuildJobs' '$hash'"))
if ($PrepareOnly) { Write-Host 'Billing prepared. Create all four DNS A records, then rerun without -PrepareOnly.' }
else {
    foreach ($domain in $domains) {
        $path = if ($domain -like 'ev.*.api.*') { '/health/ready' } else { '/' }
        $page = Invoke-WebRequest -UseBasicParsing -Uri "https://$domain$path" -TimeoutSec 20
        if ($page.StatusCode -ne 200) { throw "Public HTTPS check failed: $domain" }
        Write-Host "Deployed: https://$domain/"
    }
}
