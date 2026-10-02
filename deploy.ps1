#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('All','Ocpp','Billing')][string]$Target = 'All',
    [ValidatePattern('^[a-z0-9]+([.-][a-z0-9]+)*\.[a-z]{2,}$')]
    [ValidateLength(4,180)][string]$BaseDomain = 'barryofeverything.com',
    [string]$KeyPath = '',
    [ValidateRange(1,4)][int]$BuildJobs = 1,
    [switch]$PrepareOnly,
    [switch]$PackageOnly,
    [switch]$ApplyMigrations
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = @{ BuildJobs=$BuildJobs; PrepareOnly=$PrepareOnly; PackageOnly=$PackageOnly }
if ($KeyPath) { $common.KeyPath = $KeyPath }
if (-not $PrepareOnly -and -not $PackageOnly) {
    $labels = @()
    if ($Target -in 'All','Ocpp') { $labels += 'ocpp' }
    if ($Target -in 'All','Billing') { $labels += 'ev.admin','ev.admin.api','ev.customer','ev.customer.api' }
    foreach ($label in $labels) {
        $domain = "$label.$BaseDomain"; $addresses = @()
        try { $addresses = @([Net.Dns]::GetHostAddresses($domain) | ForEach-Object { $_.IPAddressToString }) } catch { }
        if ($addresses.Count -eq 0 -or @($addresses | Where-Object { $_ -ne '104.248.96.73' }).Count) {
            throw "DNS not ready: A $label -> 104.248.96.73 in $BaseDomain. No deployment started. Use -PrepareOnly while waiting."
        }
    }
}
# Build billing before activating any services, so frontend errors are discovered first.
if ($Target -in 'All','Billing') { & (Join-Path $PSScriptRoot 'billing_management/scripts/build-frontend.ps1') }
if ($Target -in 'All','Ocpp') {
    & (Join-Path $PSScriptRoot 'ocpp_csms/deploy.ps1') @common -Domain "ocpp.$BaseDomain" -ApplyMigrations:$ApplyMigrations
}
if ($Target -in 'All','Billing') {
    & (Join-Path $PSScriptRoot 'billing_management/deploy.ps1') @common -BaseDomain $BaseDomain -SkipFrontendBuild
}
