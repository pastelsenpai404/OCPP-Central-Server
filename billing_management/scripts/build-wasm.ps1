#requires -Version 5.1
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$localSdkEnvironment = Join-Path $root '.deps/emsdk/emsdk_env.ps1'
if (-not (Get-Command emcmake -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath $localSdkEnvironment)) {
    $previousQuiet = [Environment]::GetEnvironmentVariable('EMSDK_QUIET', 'Process')
    try {
        $env:EMSDK_QUIET = '1'
        . $localSdkEnvironment *> $null
    } finally { [Environment]::SetEnvironmentVariable('EMSDK_QUIET', $previousQuiet, 'Process') }
}
# Visual Studio bundles both CMake and Ninja; use them when they are not on PATH.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $cmakeTools = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake'
        $env:PATH = "$cmakeTools/CMake/bin;$cmakeTools/Ninja;$env:PATH"
    }
}
foreach ($tool in @('emcmake', 'cmake', 'ninja')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing $tool. Activate the Emscripten SDK environment first."
    }
}
$build = Join-Path $root 'build/wasm'
# The SDK's PowerShell wrapper sets Stop internally even for diagnostic stderr.
# Use its native batch entrypoint and check the actual process exit code.
$emcmake = Get-Command 'emcmake.bat' -ErrorAction Stop
$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $emcmake.Source cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 |
        ForEach-Object { Write-Host $_.ToString() }
    $configureExit = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedPreference }
if ($configureExit) { throw 'WASM configure failed.' }
& cmake --build $build --parallel 2
if ($LASTEXITCODE) { throw 'WASM build failed.' }
Write-Host "Management: $build/management/frontend/dist"
Write-Host "Customer:   $build/customer/frontend/dist"
