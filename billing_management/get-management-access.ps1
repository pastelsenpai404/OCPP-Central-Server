#requires -Version 5.1
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'management/access.local.json'
$command = Get-Command plink.exe -ErrorAction SilentlyContinue
$plink = if ($command) { $command.Source } else { Join-Path $env:ProgramFiles 'PuTTY/plink.exe' }
if (-not (Test-Path -LiteralPath $plink)) { throw 'Install PuTTY before fetching management access.' }
# Return only the management admin token and public API host, never the whole environment.
$reader = 'import json; from pathlib import Path; d=dict(line.split("=",1) for line in Path("/etc/billing-management/management.env").read_text().splitlines() if line and not line.startswith("#")); print(json.dumps({"token":d["BILLING_API_TOKEN"],"host":d["BILLING_API_HOST"]}))'
$saved = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $output = $reader | & $plink -batch -T -agent -hostkey 'SHA256:7Zda6mpMXh05H2Pk8QEdHLdQ3comzT/3UdGSMcWoovg' root@104.248.96.73 'python3 -' 2>$null
    $code = $LASTEXITCODE
} finally { $ErrorActionPreference = $saved }
if ($code -ne 0) { throw 'SSH access failed. Unlock the existing server key in Pageant, then rerun. No access file was changed.' }
try { $access = ($output -join "`n") | ConvertFrom-Json } catch { throw 'Server returned an invalid access response.' }
if ($access.token -notmatch '^[a-f0-9]{64}$' -or $access.host -ne 'ev.admin.api.barryofeverything.com') {
    throw 'Unexpected management credential format or API host. No access file was changed.'
}
$data = [ordered]@{
    api_endpoint = ('https://' + $access.host)
    api_token = $access.token
    ui_url = 'https://ev.admin.barryofeverything.com'
    role = 'admin'
}
$null = New-Item -ItemType File -Path $target -Force
# Restrict this credential file to the Windows identity running the script.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = New-Object Security.AccessControl.FileSecurity
$acl.SetAccessRuleProtection($true,$false)
$rule = New-Object Security.AccessControl.FileSystemAccessRule($identity,'FullControl','Allow')
$acl.AddAccessRule($rule)
Set-Acl -LiteralPath $target -AclObject $acl
[IO.File]::WriteAllText($target,($data | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
Write-Host "Access saved: $target"
Write-Host 'Copy api_token into the EV Admin Access token field. This file is ignored by Git.'
