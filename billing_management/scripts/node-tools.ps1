function Use-BillingNode([string]$Root) {
    $node = Get-Command node.exe -ErrorAction SilentlyContinue
    $version = if ($node) { [version]((& $node.Source --version).TrimStart('v')) } else { [version]'0.0' }
    if ($version -lt [version]'22.17.0') {
        $sdk = Join-Path $Root '.deps/emsdk/node'
        $candidate = if (Test-Path -LiteralPath $sdk) {
            Get-ChildItem -LiteralPath $sdk -Directory | Sort-Object Name -Descending |
                Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'node.exe') } | Select-Object -First 1
        } else { $null }
        if (-not $candidate) { throw 'SvelteKit requires Node.js 22.17+ (Node 24 LTS recommended).' }
        $env:PATH = $candidate.FullName + ';' + $env:PATH
    }
    if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) { throw 'Install npm with Node.js.' }
}

function Invoke-BillingNpm([string[]]$Arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & npm.cmd @Arguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($code) { throw "npm $($Arguments -join ' ') failed (exit $code)." }
}
