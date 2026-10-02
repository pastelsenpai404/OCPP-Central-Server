#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('management', 'customer')][string]$Area = 'management',
    [switch]$BuildWasm
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'node-tools.ps1')
Use-BillingNode $root
if ($BuildWasm -or -not (Test-Path -LiteralPath (Join-Path $root "build/wasm/$Area/frontend/dist/billing.wasm"))) {
    & (Join-Path $PSScriptRoot 'build-wasm.ps1')
}
Push-Location $root
try {
    if (-not (Test-Path -LiteralPath 'node_modules')) {
        Invoke-BillingNpm -Arguments @('ci')
    }
    Invoke-BillingNpm -Arguments @('run','wasm:sync')
    Invoke-BillingNpm -Arguments @('run',"dev:$Area")
} finally { Pop-Location }
