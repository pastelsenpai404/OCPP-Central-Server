#requires -Version 5.1
[CmdletBinding()]
param([switch]$SkipBuild,[switch]$NoBrowser)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$configPath=Join-Path $PSScriptRoot 'config.local.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    $bytes=New-Object byte[] 32; $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    $config=Get-Content (Join-Path $PSScriptRoot 'config.local.json.example') -Raw | ConvertFrom-Json
    $config.SIM_ADMIN_TOKEN=([BitConverter]::ToString($bytes)).Replace('-','').ToLowerInvariant()
    $config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding UTF8
    Write-Host 'Created private config.local.json. Copy SIM_ADMIN_TOKEN from this file into the login screen.'
}
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'scripts/build-native.ps1')
    & (Join-Path $PSScriptRoot 'scripts/build-frontend.ps1')
}
$config=Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$saved=@{}
try {
    foreach ($property in $config.PSObject.Properties) {
        if ($property.Name -notin @('SIM_PORT','SIM_API_HOST','SIM_PUBLIC_ORIGIN','SIM_ADMIN_TOKEN','SIM_CSMS_URL','SIM_STATION_SECRETS','SIM_CA_FILE')) { throw 'Unknown config property.' }
        $saved[$property.Name]=[Environment]::GetEnvironmentVariable($property.Name,'Process')
        $value=if ($property.Name -eq 'SIM_STATION_SECRETS') { ConvertTo-Json -InputObject $property.Value -Compress -Depth 4 } else { [string]$property.Value }
        [Environment]::SetEnvironmentVariable($property.Name,$value,'Process')
    }
    $saved.SIM_WEB_ROOT=$env:SIM_WEB_ROOT
    $env:SIM_WEB_ROOT=Join-Path $PSScriptRoot 'frontend/dist'
    $binary=Join-Path $PSScriptRoot 'build/native/backend/Release/ev_charger_server.exe'
    if (-not (Test-Path -LiteralPath $binary)) { throw 'Build native server first.' }
    $url="http://127.0.0.1:$($config.SIM_PORT)/"
    Write-Host "Workspace: $url  (token: config.local.json > SIM_ADMIN_TOKEN)"
    if (-not $NoBrowser) { Start-Process $url }
    & $binary
    if ($LASTEXITCODE) { throw 'Simulator exited with an error.' }
} finally { foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') } }
