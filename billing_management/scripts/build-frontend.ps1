#requires -Version 5.1
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build-wasm.ps1')
. (Join-Path $PSScriptRoot 'node-tools.ps1')
Use-BillingNode $root
Push-Location $root
try {
    Invoke-BillingNpm -Arguments @('ci')
    Invoke-BillingNpm -Arguments @('run','test:wasm')
    Invoke-BillingNpm -Arguments @('run','check')
    Invoke-BillingNpm -Arguments @('run','build')
} finally { Pop-Location }
