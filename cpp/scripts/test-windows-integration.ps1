[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$MariaDbDirectory,[string]$Python='python',[ValidateSet('Release','Debug')][string]$Configuration='Release',[ValidateRange(0,200)][int]$LoadStations=0)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$MariaDbDirectory=(Resolve-Path -LiteralPath $MariaDbDirectory).Path
$data=Join-Path $Root ('.deps/test-data-'+[guid]::NewGuid().ToString('N'))
$bytes=New-Object byte[] 32
$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
$rng.GetBytes($bytes);$rng.Dispose()
$password=([BitConverter]::ToString($bytes)).Replace('-','').ToLowerInvariant()
function Free-Port {
  $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
  $listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop();return $port
}
$dbPort=Free-Port
$serverPort=Free-Port
$install=Join-Path $MariaDbDirectory 'bin/mariadb-install-db.exe'
& $install "--datadir=$data" "--password=$password" "--port=$dbPort" --silent
if($LASTEXITCODE){throw 'Test DB initialization failed'}
$process=Start-Process -FilePath (Join-Path $MariaDbDirectory 'bin/mariadbd.exe') -ArgumentList @("--defaults-file=`"$data/my.ini`"",'--bind-address=127.0.0.1',"--port=$dbPort",'--console') -PassThru -WindowStyle Hidden -RedirectStandardOutput "$data/stdout.log" -RedirectStandardError "$data/stderr.log"
$previous=$env:OCPP_TEST_DB_PASSWORD
try {
  $ready=$false
  for($i=0;$i -lt 100;$i++) {
    if($process.HasExited){throw 'Test database exited'}
    $tcp=[Net.Sockets.TcpClient]::new()
    try{$tcp.Connect('127.0.0.1',$dbPort);$ready=$true;break}catch{Start-Sleep -Milliseconds 100}finally{$tcp.Dispose()}
  }
  if(-not $ready){throw 'Test DB did not become ready'}
  $env:OCPP_TEST_DB_PASSWORD=$password
  $savedPreference=$ErrorActionPreference
  $ErrorActionPreference='Continue'
  try {
    & $Python -u "$Root/tests/integration.py" --server "$Root/build/$Configuration/ocpp_server.exe" --db-port $dbPort --port $serverPort --load-stations $LoadStations
    $testCode=$LASTEXITCODE
  } finally { $ErrorActionPreference=$savedPreference }
  if($testCode){throw 'Integration tests failed'}
} finally {
  $env:OCPP_TEST_DB_PASSWORD=$previous
  if(-not $process.HasExited){Stop-Process -Id $process.Id; $process.WaitForExit()}
}
