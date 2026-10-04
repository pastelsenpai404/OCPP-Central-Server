#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build-wasm.ps1')
$wasm=Join-Path $root 'frontend/static/wasm'
$null=New-Item -ItemType Directory -Force -Path $wasm
Copy-Item -LiteralPath (Join-Path $root 'build/wasm/wasm/physics.js') -Destination $wasm
Copy-Item -LiteralPath (Join-Path $root 'build/wasm/wasm/physics.wasm') -Destination $wasm
. (Join-Path $PSScriptRoot 'node-tools.ps1')
Use-SimulationNode
Push-Location (Join-Path $root 'frontend')
try { Invoke-SimulationNpm @('ci'); Invoke-SimulationNpm @('run','check'); Invoke-SimulationNpm @('run','build') } finally {Pop-Location}
$html=[IO.File]::ReadAllText((Join-Path $root 'frontend/dist/index.html'))
$hashes=@()
foreach($match in [regex]::Matches($html,'<script\b([^>]*)>(.*?)</script>','Singleline')) {
    if($match.Groups[1].Value -match '\bsrc\s*='){continue}
    $sha=[Security.Cryptography.SHA256]::Create()
    try {$hash=[Convert]::ToBase64String($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($match.Groups[2].Value)))} finally {$sha.Dispose()}
    $hashes+="'sha256-$hash'"
}
$policy="default-src 'self'; script-src 'self' 'wasm-unsafe-eval' $($hashes -join ' '); style-src 'self' 'unsafe-inline'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'"
[IO.File]::WriteAllText((Join-Path $root 'frontend/dist/csp.txt'),$policy,(New-Object Text.UTF8Encoding($false)))
$licenses=Join-Path $root 'frontend/dist/licenses'
$null=New-Item -ItemType Directory -Force -Path $licenses
$modules=Join-Path $root 'frontend/node_modules'
$packages=@(Get-ChildItem -LiteralPath $modules -Directory | Where-Object {$_.Name -notlike '.*'})
foreach($scope in @($packages | Where-Object {$_.Name.StartsWith('@')})) {
    $packages+=@(Get-ChildItem -LiteralPath $scope.FullName -Directory)
}
foreach($package in $packages) {
    $name=$package.FullName.Substring($modules.Length+1).Replace('\','_').Replace('/','_').Replace('@','')
    foreach($file in @(Get-ChildItem -LiteralPath $package.FullName -File | Where-Object {$_.Name -match '^(LICENSE|LICENCE|COPYING)(\..*)?$'})) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $licenses "$name-$($file.Name).txt")
    }
}
Write-Host 'Frontend and WASM ready.'
