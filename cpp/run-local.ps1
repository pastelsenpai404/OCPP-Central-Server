#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$ConfigPath = '',
    [string]$KeyPath = '',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Build
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $ConfigPath) { $ConfigPath = Join-Path $PSScriptRoot 'config.local.json' }
if (-not $KeyPath) { $KeyPath = Join-Path $PSScriptRoot '../material/nene_key_private.ppk' }
$ConfigPath = (Resolve-Path -LiteralPath $ConfigPath).ProviderPath
$KeyPath = (Resolve-Path -LiteralPath $KeyPath).ProviderPath
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
if ($config.OCPP_DB_HOST -cne '127.0.0.1' -or -not $config.OCPP_DB_CA) {
    throw 'Use the prepared tunnel configuration: OCPP_DB_HOST=127.0.0.1 and a verified OCPP_DB_CA.'
}
$dbPort = [int]$config.OCPP_DB_PORT
$serverPort = [int]$config.OCPP_PORT
if ($dbPort -eq $serverPort) { throw 'The server port and database tunnel port must differ.' }
$serverScript = Join-Path $PSScriptRoot 'run-server.ps1'
& $serverScript -ConfigPath $ConfigPath -Configuration $Configuration -CheckConfig -Build:$Build

$tool = Get-Command plink.exe -ErrorAction SilentlyContinue
$plink = if ($tool) { $tool.Source } else { Join-Path $env:ProgramFiles 'PuTTY/plink.exe' }
if (-not (Test-Path -LiteralPath $plink -PathType Leaf)) { throw 'Install PuTTY (plink.exe) first.' }
foreach ($port in @($dbPort, $serverPort)) {
    $probe = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $port)
    try { $probe.Start() }
    catch { throw "Local port $port is already in use. Stop the existing server/tunnel first." }
    finally { $probe.Stop() }
}

function Test-Tunnel {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $pending = $client.BeginConnect('127.0.0.1', $dbPort, $null, $null)
        if (-not $pending.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($pending)
        return $true
    } catch { return $false }
    finally { $client.Dispose() }
}
function Test-Ready {
    try {
        $request = [Net.HttpWebRequest]::Create("http://127.0.0.1:$serverPort/health/ready")
        $request.Proxy = $null
        $request.Timeout = 1000
        $request.ReadWriteTimeout = 1000
        $response = $request.GetResponse()
        try {
            $reader = New-Object IO.StreamReader($response.GetResponseStream())
            try { return (($reader.ReadToEnd() | ConvertFrom-Json).status -ceq 'ready') }
            finally { $reader.Dispose() }
        } finally { $response.Dispose() }
    } catch { return $false }
}
# Start-Process joins its argument array on Windows. Quote resolved file paths.
function Quote-Path([string]$Path) { return '"' + $Path + '"' }
# A Windows job owns both process trees, including the native server beneath
# PowerShell. Closing this console also closes the job and releases its children.
if (-not ('OcppLocalProcessGroup' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class OcppLocalProcessGroup : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Basic {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct Io {
        public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
    }
    [StructLayout(LayoutKind.Sequential)] struct Limits {
        public Basic Basic;
        public Io Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    delegate bool Control(uint kind);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job, int kind, ref Limits limits, uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetConsoleCtrlHandler(Control handler, bool add);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    IntPtr job;
    Control control;
    public volatile bool StopRequested;
    public OcppLocalProcessGroup() {
        job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) throw new Win32Exception();
        var limits = new Limits();
        limits.Basic.Flags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(Limits)))) {
            int error = Marshal.GetLastWin32Error();
            CloseHandle(job); job = IntPtr.Zero;
            throw new Win32Exception(error);
        }
        control = delegate(uint kind) {
            if (kind != 0 && kind != 1) return false;
            StopRequested = true;
            return true;
        };
        // Clear an inherited ignore-Ctrl+C flag (some terminal hosts set it).
        if (!SetConsoleCtrlHandler(null, false) || !SetConsoleCtrlHandler(control, true)) {
            int error = Marshal.GetLastWin32Error();
            CloseHandle(job); job = IntPtr.Zero;
            throw new Win32Exception(error);
        }
    }
    public void Own(IntPtr process) {
        if (!AssignProcessToJobObject(job, process)) throw new Win32Exception();
    }
    public void Dispose() {
        if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
        if (control != null) { SetConsoleCtrlHandler(control, false); control = null; }
    }
}
'@
}
$group = New-Object OcppLocalProcessGroup
$tunnel = $null
$server = $null
try {
    Write-Host "Opening database tunnel to 104.248.96.73 (local port $dbPort)."
    Write-Host 'Enter your SSH key passphrase at the PuTTY prompt below. It is not stored.'
    # Authentication needs the same interactive console; no extra window is opened.
    $tunnel = Start-Process -FilePath $plink -NoNewWindow -PassThru -ArgumentList @(
        '-ssh', '-N', '-no-antispoof', '-i', (Quote-Path $KeyPath),
        '-hostkey', 'SHA256:7Zda6mpMXh05H2Pk8QEdHLdQ3comzT/3UdGSMcWoovg',
        '-L', "127.0.0.1:${dbPort}:127.0.0.1:3307", 'root@104.248.96.73'
    )
    $group.Own($tunnel.Handle)
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while (-not (Test-Tunnel)) {
        if ($group.StopRequested) { return }
        if ($tunnel.HasExited) { throw "SSH tunnel exited (code $($tunnel.ExitCode))." }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'SSH authentication/tunnel timed out.' }
        Start-Sleep -Milliseconds 250
    }
    if ($group.StopRequested) { return }
    $powershell = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $powershell)) { $powershell = Join-Path $PSHOME 'pwsh.exe' }
    $server = Start-Process -FilePath $powershell -NoNewWindow -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Quote-Path $serverScript),
        '-ConfigPath', (Quote-Path $ConfigPath), '-Configuration', $Configuration
    )
    $group.Own($server.Handle)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $ready = $false
    while (-not $server.HasExited) {
        if ($group.StopRequested) { return }
        if ($tunnel.HasExited) { throw 'SSH connection closed. Stopping the local server.' }
        if (-not $ready) {
            if (Test-Ready) {
                $ready = $true
                Write-Host "READY: local OCPP server with remote DB over SSH and verified TLS."
                Write-Host "Home:   http://127.0.0.1:$serverPort/"
                Write-Host "Health: http://127.0.0.1:$serverPort/health/ready"
                Write-Host "OCPP:   ws://127.0.0.1:$serverPort/ocpp/CP01 (station credentials required)"
                Write-Host 'Keep this console open. Ctrl+C stops the server and its tunnel.'
            } elseif ([DateTime]::UtcNow -ge $deadline) { throw 'Server readiness timed out. See startup output above.' }
        }
        Start-Sleep -Milliseconds 500
    }
    if ($server.ExitCode -ne 0) { throw "Server launcher exited (code $($server.ExitCode))." }
} finally {
    $group.Dispose()
    if ($null -ne $server) {
        # Stop only native children owned by the PowerShell instance we started.
        if (-not $server.HasExited) {
            Get-CimInstance Win32_Process -Filter "ParentProcessId=$($server.Id)" |
                Where-Object { $_.Name -eq 'ocpp_server.exe' } |
                ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
            Stop-Process -Id $server.Id -ErrorAction SilentlyContinue
            $server.WaitForExit()
        }
        $server.Dispose()
    }
    if ($null -ne $tunnel) {
        if (-not $tunnel.HasExited) {
            Stop-Process -Id $tunnel.Id -ErrorAction SilentlyContinue
            $tunnel.WaitForExit()
        }
        $tunnel.Dispose()
    }
}
