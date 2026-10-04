#requires -Version 5.1
[CmdletBinding()]
param([switch]$SkipBuild, [switch]$NoBrowser)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$configuration = Join-Path $root 'management/config.local.json'
function New-Token {
    $bytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    return -join ($bytes | ForEach-Object { $_.ToString('x2') })
}
if (-not (Test-Path -LiteralPath $configuration)) {
    $values = [ordered]@{
        BILLING_PORT = 5500; BILLING_API_HOST = '127.0.0.1:5500'
        BILLING_ALLOWED_ORIGIN = 'http://127.0.0.1:5100'
        BILLING_API_TOKEN = (New-Token); BILLING_READER_TOKEN = (New-Token)
        BILLING_OPERATOR_TOKEN = (New-Token)
        BILLING_DATABASE_PATH = 'build/local-state/backoffice.sqlite3'
    }
    [IO.File]::WriteAllText($configuration, ($values | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
}
$settings = Get-Content -LiteralPath $configuration -Raw | ConvertFrom-Json
$listeners = @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.LocalPort -in @([int]$settings.BILLING_PORT,5100) })
if ($listeners.Count) { throw 'Management API/frontend port is already in use. Stop the existing local instance first.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$build = Join-Path $root 'build/http'
if (-not $SkipBuild) {
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
    $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    & $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 -DBILLING_BUILD_HTTP=ON
    if ($LASTEXITCODE) { throw 'C++ configure failed.' }
    & $cmake --build $build --config Release --parallel 2
    if ($LASTEXITCODE) { throw 'C++ build failed.' }
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Management checks failed.' }
}
$binary = Join-Path $build 'management/backend/Release/billing_management_api.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Management API binary missing. Run without -SkipBuild.' }
. (Join-Path $root 'scripts/node-tools.ps1')
Use-BillingNode $root
if (-not (Test-Path -LiteralPath (Join-Path $root 'node_modules'))) {
    Push-Location $root
    try { Invoke-BillingNpm @('ci') } finally { Pop-Location }
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'management/frontend/static/wasm/billing.wasm'))) {
    & (Join-Path $root 'scripts/build-wasm.ps1')
}
$database = [string]$settings.BILLING_DATABASE_PATH
if (-not [IO.Path]::IsPathRooted($database)) { $database = Join-Path $root $database }
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $database) -Force
$frontend = $null
$previous = @{}
try {
    foreach ($property in $settings.PSObject.Properties) {
        if ($property.Name -notlike 'BILLING_*') { throw 'Unknown local configuration key.' }
        $previous[$property.Name] = [Environment]::GetEnvironmentVariable($property.Name,'Process')
        $value = if ($property.Name -eq 'BILLING_DATABASE_PATH') { $database } else { [string]$property.Value }
        [Environment]::SetEnvironmentVariable($property.Name,$value,'Process')
    }
    $vite = Join-Path $root 'node_modules/vite/bin/vite.js'
    $frontend = Start-Process -FilePath (Get-Command node).Source -ArgumentList @(('"'+$vite+'"'),'dev','--host','127.0.0.1','--port','5100','--strictPort') -WorkingDirectory (Join-Path $root 'management/frontend') -WindowStyle Hidden -PassThru
    Write-Host 'Management UI: http://127.0.0.1:5100'
    Write-Host "API token: BILLING_API_TOKEN in $configuration (ignored by Git)."
    Write-Host "Sandbox database: $database"
    if (-not $NoBrowser) { Start-Process 'http://127.0.0.1:5100' }
    & $binary
    if ($LASTEXITCODE) { throw 'Management server exited with an error.' }
} finally {
    if ($frontend -and -not $frontend.HasExited) { Stop-Process -Id $frontend.Id }
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
}
