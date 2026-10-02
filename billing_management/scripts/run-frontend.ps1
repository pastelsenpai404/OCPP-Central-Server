#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('management', 'customer')][string]$Area = 'management',
    [switch]$BuildWasm
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# Use the locally installed SDK Node when the system Node is older than Kit needs.
$node = Get-Command node.exe -ErrorAction SilentlyContinue
$nodeVersion = if ($node) { [version]((& $node.Source --version).TrimStart('v')) } else { [version]'0.0' }
if ($nodeVersion -lt [version]'22.17.0') {
    $sdkNode = Join-Path $root '.deps/emsdk/node'
    $candidate = if (Test-Path -LiteralPath $sdkNode) {
        Get-ChildItem -LiteralPath $sdkNode -Directory | Sort-Object Name -Descending |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'node.exe') } | Select-Object -First 1
    } else { $null }
    if (-not $candidate) { throw 'SvelteKit requires Node.js 22.17+ (Node 24 LTS recommended).' }
    $env:PATH = $candidate.FullName + ';' + $env:PATH
}
if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) { throw 'Install npm with Node.js.' }
if ($BuildWasm -or -not (Test-Path -LiteralPath (Join-Path $root "build/wasm/$Area/frontend/dist/billing.wasm"))) {
    & (Join-Path $PSScriptRoot 'build-wasm.ps1')
}
Push-Location $root
try {
    if (-not (Test-Path -LiteralPath 'node_modules')) {
        & npm.cmd ci
        if ($LASTEXITCODE) { throw 'npm ci failed.' }
    }
    & npm.cmd run wasm:sync
    if ($LASTEXITCODE) { throw 'WASM sync failed.' }
    & npm.cmd run "dev:$Area"
    if ($LASTEXITCODE) { throw 'Frontend dev server failed.' }
} finally { Pop-Location }
