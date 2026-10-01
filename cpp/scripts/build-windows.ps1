[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ Build Tools required'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$prefix=Join-Path $Root '.deps/install'
$zlibName=if($Configuration -eq 'Debug'){'zd'}else{'z'}
function Prepare-BuildDirectory([string]$Path, [string]$Source) {
  $cache = Join-Path $Path 'CMakeCache.txt'
  if (-not (Test-Path -LiteralPath $cache)) { return }
  $line = Get-Content -LiteralPath $cache | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' } | Select-Object -First 1
  if (-not $line) { return }
  $cachedSource = $line.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length).Replace('/', '\')
  if ([IO.Path]::GetFullPath($cachedSource) -eq [IO.Path]::GetFullPath($Source)) { return }
  $absoluteRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $absoluteTarget = (Resolve-Path -LiteralPath $Path).ProviderPath
  if (-not $absoluteTarget.StartsWith($absoluteRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Build directory is outside this project.' }
  $archiveName = (Split-Path -Leaf $absoluteTarget) + '-before-relocation-' + [Guid]::NewGuid().ToString('N')
  Write-Host "Preserving relocated build cache as $archiveName"
  Rename-Item -LiteralPath $absoluteTarget -NewName $archiveName
}
function Run-CMake([string[]]$Arguments){
  $savedPreference=$ErrorActionPreference
  $ErrorActionPreference='Continue'
  & $cmake @Arguments
  $code=$LASTEXITCODE
  $ErrorActionPreference=$savedPreference
  if($code){throw "CMake failed: $code"}
}
function Clone-Pinned($Name,$Url,$Commit){
  $path=Join-Path $Root ".deps/$Name"
  if(-not(Test-Path "$path/CMakeLists.txt")){ & git clone $Url $path; if($LASTEXITCODE){throw 'clone failed'} }
  $savedPreference=$ErrorActionPreference
  $ErrorActionPreference='Continue'
  & git -C $path checkout --detach $Commit
  $ErrorActionPreference=$savedPreference
  if($LASTEXITCODE){throw 'checkout failed'}
}
Clone-Pinned jsoncpp https://github.com/open-source-parsers/jsoncpp.git 89e2973c754a9c02a49974d839779b151e95afd6
Clone-Pinned zlib https://github.com/madler/zlib.git da607da739fa6047df13e66a2af6b8bec7c2a498
Clone-Pinned mariadb https://github.com/mariadb-corporation/mariadb-connector-c.git c61bdb5ac1cd1b41210dd57bb14fa377e555ce0c
foreach($dep in @('jsoncpp','zlib','mariadb')){
  Prepare-BuildDirectory "$Root/.deps/build-$dep" "$Root/.deps/$dep"
  $args=@('-S',"$Root/.deps/$dep",'-B',"$Root/.deps/build-$dep",'-G','Visual Studio 17 2022','-A','x64',"-DCMAKE_INSTALL_PREFIX=$prefix",'-DBUILD_SHARED_LIBS=OFF')
  if($dep -eq 'jsoncpp'){$args+=@('-DJSONCPP_WITH_TESTS=OFF','-DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF','-DJSONCPP_WITH_EXAMPLE=OFF')}
  if($dep -eq 'zlib'){$args+=@('-DZLIB_BUILD_TESTING=OFF')}
  if($dep -eq 'mariadb'){$args+=@('-DWITH_UNIT_TESTS=OFF','-DWITH_CURL=OFF','-DWITH_SSL=SCHANNEL','-DINSTALL_LAYOUT=DEFAULT','-DWITH_EXTERNAL_ZLIB=ON',"-DCMAKE_PREFIX_PATH=$prefix","-DZLIB_LIBRARY_RELEASE=$prefix/lib/$zlibName.lib","-DZLIB_LIBRARY_DEBUG=$prefix/lib/$zlibName.lib","-DZLIB_LIBRARY=$prefix/lib/$zlibName.lib","-DZLIB_INCLUDE_DIR=$prefix/include")}
  Run-CMake $args
  Run-CMake @('--build',"$Root/.deps/build-$dep",'--config',$Configuration,'--parallel','4')
  Run-CMake @('--install',"$Root/.deps/build-$dep",'--config',$Configuration)
}
$args=@('-S',$Root,'-B',"$Root/build",'-G','Visual Studio 17 2022','-A','x64',"-DCMAKE_PREFIX_PATH=$prefix",'-DCMAKE_POLICY_VERSION_MINIMUM=3.5',"-DZLIB_LIBRARY_RELEASE=$prefix/lib/$zlibName.lib","-DZLIB_LIBRARY_DEBUG=$prefix/lib/$zlibName.lib","-DZLIB_LIBRARY=$prefix/lib/$zlibName.lib","-DZLIB_INCLUDE_DIR=$prefix/include")
Prepare-BuildDirectory "$Root/build" $Root
foreach($pair in @(@('json','json'),@('schema_validator','schema-validator'),@('drogon','drogon'),@('pugixml','pugixml'))){
  $path=Join-Path $Root ".deps/$($pair[1])"
  if(Test-Path "$path/CMakeLists.txt"){$args+="-DFETCHCONTENT_SOURCE_DIR_$($pair[0].ToUpper())=$path"}
}
Run-CMake $args
Run-CMake @('--build',"$Root/build",'--config',$Configuration,'--parallel','4')
foreach($name in @('libmariadb.dll',"$zlibName.dll")){
  Get-ChildItem $prefix -Recurse -Filter $name | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination "$Root/build/$Configuration"}
}
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$Root/build" -C $Configuration --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
