#requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ Build Tools with CMake are required.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools are required.' }
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Install the CMake component in Visual Studio Build Tools.' }
$build = Join-Path $root 'build/native'
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'Native configure failed.' }
& $cmake --build $build --config $Configuration --parallel 2
if ($LASTEXITCODE) { throw 'Native build failed.' }
& $ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Native tests failed.' }
Write-Host "Executables: $build/management/backend/$Configuration and $build/customer/backend/$Configuration"
