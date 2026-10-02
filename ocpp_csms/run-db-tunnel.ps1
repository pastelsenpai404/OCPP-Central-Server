#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateRange(1, 65535)][int]$LocalPort = 13307,
    [string]$KeyPath = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($KeyPath)) { $KeyPath = Join-Path $PSScriptRoot '../material/nene_key_private.ppk' }
$KeyPath = (Resolve-Path -LiteralPath $KeyPath).ProviderPath
$tool = Get-Command plink.exe -ErrorAction SilentlyContinue
if ($tool) { $plink = $tool.Source }
else { $plink = Join-Path $env:ProgramFiles 'PuTTY/plink.exe' }
if (-not (Test-Path -LiteralPath $plink -PathType Leaf)) { throw 'Install PuTTY (plink.exe) first.' }
$listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $LocalPort)
try { $listener.Start() }
catch { throw "Local port $LocalPort is already in use. Stop the other tunnel or choose -LocalPort." }
finally { $listener.Stop() }
Write-Host "Opening SSH tunnel: 127.0.0.1:$LocalPort -> remote MariaDB 127.0.0.1:3307."
Write-Host 'Enter the key passphrase when prompted. Keep this console open; run run-server.ps1 in a second console.'
Write-Host 'Ctrl+C closes the tunnel. No password or passphrase is stored by this script.'
& $plink -ssh -N -no-antispoof -i $KeyPath `
    -hostkey 'SHA256:7Zda6mpMXh05H2Pk8QEdHLdQ3comzT/3UdGSMcWoovg' `
    -L "127.0.0.1:${LocalPort}:127.0.0.1:3307" root@104.248.96.73
if ($LASTEXITCODE -ne 0) { throw "SSH tunnel exited with code $LASTEXITCODE." }
