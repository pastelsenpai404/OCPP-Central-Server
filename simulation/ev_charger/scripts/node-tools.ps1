function Use-SimulationNode {
    $node=Get-Command node.exe -ErrorAction SilentlyContinue
    $version=if($node){[version]((& $node.Source --version).TrimStart('v'))}else{[version]'0.0'}
    if($version -lt [version]'22.17.0') {
        $roots=@((Join-Path $PSScriptRoot '../.deps/emsdk/node'),(Join-Path $PSScriptRoot '../../../billing_management/.deps/emsdk/node'))
        $candidate=$null
        foreach($sdk in $roots) {if(Test-Path -LiteralPath $sdk){$candidate=Get-ChildItem -LiteralPath $sdk -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'node.exe')} | Sort-Object Name -Descending | Select-Object -First 1;if($candidate){break}}}
        if(-not $candidate){throw 'Install Node.js 22.17+ or activate Emscripten SDK.'}
        $env:PATH=$candidate.FullName+';'+$env:PATH
    }
}
function Invoke-SimulationNpm([string[]]$Arguments) {
    $saved=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try { & npm.cmd @Arguments 2>&1 | ForEach-Object {Write-Host $_.ToString()};$code=$LASTEXITCODE } finally {$ErrorActionPreference=$saved}
    if($code){throw "npm failed (exit $code)."}
}
