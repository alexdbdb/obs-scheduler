param(
  [string]$Configuration = 'Release',
  [switch]$SkipInstaller
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$repo = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $repo '.deps'
$prefix = Join-Path $deps 'sdk'
New-Item -ItemType Directory -Force $deps, $prefix | Out-Null
function Run([string]$Program, [string[]]$Arguments) {
  & $Program @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Archive([string]$Url, [string]$File, [string]$Destination, [string]$Hash = '') {
  if (!(Test-Path $File)) { Invoke-WebRequest $Url -OutFile $File }
  if ($Hash -and (Get-FileHash $File -Algorithm SHA256).Hash -ne $Hash) { throw "Checksum mismatch: $File" }
  New-Item -ItemType Directory -Force $Destination | Out-Null
  Expand-Archive -Force $File $Destination
}
# Build against the beginning of the OBS 32 series. OBS provides the Qt ABI.
$obsTag = '32.0.4'
$obs = Join-Path $deps "obs-studio-$obsTag"
if (!(Test-Path $obs)) { Run git @('clone','--depth','1','--branch',$obsTag,'https://github.com/obsproject/obs-studio.git',$obs) }
$spec = Get-Content (Join-Path $obs 'buildspec.json') -Raw | ConvertFrom-Json
foreach ($kind in @('prebuilt','qt6')) {
  $dep = $spec.dependencies.$kind
  $part = if ($kind -eq 'qt6') { 'qt6-' } else { '' }
  $file = "windows-deps-$part$($dep.version)-x64.zip"
  $destination = Join-Path $deps $kind
  if (!(Test-Path (Join-Path $destination '.ready'))) {
    Archive "$($dep.baseUrl)/$($dep.version)/$file" (Join-Path $deps $file) $destination $dep.hashes.'windows-x64'
    New-Item -ItemType File (Join-Path $destination '.ready') | Out-Null
  }
}
$qtConfig = Get-ChildItem (Join-Path $deps 'qt6') -Filter Qt6Config.cmake -Recurse | Select-Object -First 1
if (!$qtConfig) { throw 'Qt SDK not found in official dependency archive' }
$qt = (Resolve-Path (Join-Path $qtConfig.Directory.FullName '../../..')).Path
$prebuilt = Join-Path $deps 'prebuilt'
$prefixes = "$prefix;$qt;$prebuilt"
Run cmake @('-S',$obs,'-B',"$obs/build_x64",'-G','Visual Studio 17 2022','-A','x64',
  '-DENABLE_PLUGINS=OFF','-DENABLE_FRONTEND=OFF','-DENABLE_SCRIPTING=OFF',
  "-DCMAKE_PREFIX_PATH=$prefixes", "-DOBS_VERSION_OVERRIDE=$obsTag")
Run cmake @('--build',"$obs/build_x64",'--config',$Configuration,'--target','obs-frontend-api','--parallel','4')
Run cmake @('--install',"$obs/build_x64",'--config',$Configuration,'--component','Development','--prefix',$prefix)

# Additional Qt modules must use exactly the Qt shipped by the OBS SDK.
$qtVersionFile = Join-Path $qt 'lib/cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake'
if (!(Test-Path $qtVersionFile)) { $qtVersionFile = Join-Path $qt 'lib/cmake/Qt6Core/Qt6CoreConfigVersion.cmake' }
$qtVersion = [regex]::Match((Get-Content $qtVersionFile -Raw), 'set\(PACKAGE_VERSION\s+"?([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if (!$qtVersion) { throw 'Cannot determine OBS Qt version' }
foreach ($module in @(@('qtwebsockets','WebSockets'), @('qthttpserver','HttpServer'))) {
  if (Test-Path "$qt/lib/cmake/Qt6$($module[1])/Qt6$($module[1])Config.cmake") { continue }
  $source = Join-Path $deps $module[0]
  if (!(Test-Path $source)) { Run git @('clone','--depth','1','--branch',"v$qtVersion","https://github.com/qt/$($module[0]).git",$source) }
  Run cmake @('-S',$source,'-B',"$source/build",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$qt", "-DCMAKE_INSTALL_PREFIX=$qt", '-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
  Run cmake @('--build',"$source/build",'--parallel','4')
  Run cmake @('--install',"$source/build")
}
$ical = Join-Path $deps 'libical'
if (!(Test-Path $ical)) { Run git @('clone','--depth','1','--branch','v3.0.20','https://github.com/libical/libical.git',$ical) }
Run cmake @('-S',$ical,'-B',"$ical/build",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_INSTALL_PREFIX=$prefix",'-DSTATIC_ONLY=ON','-DICAL_GLIB=OFF','-DICAL_BUILD_DOCS=OFF','-DICAL_BUILD_TESTING=OFF','-DWITH_CXX_BINDINGS=OFF','-DICAL_ALLOW_EMPTY_PROPERTIES=ON','-DUSE_BUILTIN_TZDATA=ON','-DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON')
Run cmake @('--build',"$ical/build",'--parallel','4')
Run cmake @('--install',"$ical/build")
$sqlite = Join-Path $deps 'sqlite-amalgamation-3500400'
if (!(Test-Path $sqlite)) { Archive 'https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip' "$deps/sqlite.zip" $deps }
Run cmake @('-S',"$repo/cmake/sqlite",'-B',"$deps/sqlite-build",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DSQLITE_SOURCE_DIR=$sqlite", "-DCMAKE_INSTALL_PREFIX=$prefix")
Run cmake @('--build',"$deps/sqlite-build")
Run cmake @('--install',"$deps/sqlite-build")
Run cmake @('-S',$repo,'-B',"$repo/build-windows",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$prefixes",'-DBUILD_TESTING=OFF')
Run cmake @('--build',"$repo/build-windows",'--parallel','4')
$env:PATH = "$qt/bin;$prebuilt/bin;$prefix/bin;$env:PATH"
$zones = Get-ChildItem $prefix -Directory -Filter zoneinfo -Recurse | Select-Object -First 1
if (!$zones) { throw 'libical timezone data is missing' }
$env:BS_ZONEINFO = $zones.FullName
$stage = Join-Path $repo 'artifacts/windows'
Run cmake @('--install',"$repo/build-windows",'--prefix',$stage)
Copy-Item -Recurse -Force $zones.FullName "$stage/data/obs-plugins/broadcast-scheduler/"
New-Item -ItemType Directory -Force "$stage/bin/64bit" | Out-Null
foreach ($dll in @('Qt6HttpServer.dll','Qt6WebSockets.dll')) { Copy-Item "$qt/bin/$dll" "$stage/bin/64bit/" }
Copy-Item "$repo/LICENSE", "$repo/THIRD_PARTY.md" "$stage/data/obs-plugins/broadcast-scheduler/"
Copy-Item "$ical/LICENSE" "$stage/data/obs-plugins/broadcast-scheduler/LICENSE-libical" -ErrorAction SilentlyContinue
Compress-Archive -Force "$stage/*" "$repo/artifacts/broadcast-scheduler-0.1.0-windows-x64.zip"
if (!$SkipInstaller) {
  $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if (!$iscc) {
    throw 'Inno Setup 6 is required to create the user-friendly installer. Install it or pass -SkipInstaller.'
  }
  Run $iscc.Source @('/Qp', (Join-Path $repo 'installer/BroadcastScheduler.iss'))
  Write-Host "Installer ready: artifacts/Broadcast-Scheduler-0.1.0-Setup.exe"
}
Write-Host "Package ready: artifacts/broadcast-scheduler-0.1.0-windows-x64.zip (Qt $qtVersion)"
