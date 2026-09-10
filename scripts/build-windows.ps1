param(
  [string]$Configuration = 'Release',
  [string]$InstalledOBS = '',
  [switch]$SkipInstaller
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$repo = Split-Path $PSScriptRoot -Parent
$version = '0.2.0'
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
# Build against the current OBS 32 baseline used for the Windows package.
# Keeping this aligned with the current OBS 32 Qt runtime avoids loading a
# QtHttpServer/WebSockets module built against a different Qt minor release.
$obsTag = '32.2.2'
$obs = Join-Path $deps "obs-studio-$obsTag"
if (!(Test-Path $obs)) { Run git @('clone','--depth','1','--branch',$obsTag,'https://github.com/obsproject/obs-studio.git',$obs) }
$legacySpec = Join-Path $obs 'buildspec.json'
if (Test-Path $legacySpec) {
  $dependencyData = (Get-Content $legacySpec -Raw | ConvertFrom-Json).dependencies
} else {
  # OBS 32.2 moved dependency metadata into the hidden `dependencies`
  # configure preset in CMakePresets.json.
  $presetData = Get-Content (Join-Path $obs 'CMakePresets.json') -Raw | ConvertFrom-Json
  $dependencyPreset = $presetData.configurePresets |
    Where-Object { $_.name -eq 'dependencies' } |
    Select-Object -First 1
  $dependencyData = $dependencyPreset.vendor.'obsproject.com/obs-studio'.dependencies
}
if (!$dependencyData) { throw 'Cannot find OBS dependency metadata' }
foreach ($kind in @('prebuilt','qt6')) {
  $dep = $dependencyData.$kind
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
if (!$InstalledOBS) {
Run cmake @('-S',$obs,'-B',"$obs/build_x64",'-G','Visual Studio 17 2022','-A','x64',
  '-DENABLE_PLUGINS=OFF','-DENABLE_FRONTEND=OFF','-DENABLE_SCRIPTING=OFF',
  "-DCMAKE_PREFIX_PATH=$prefixes", "-DOBS_VERSION_OVERRIDE=$obsTag")
Run cmake @('--build',"$obs/build_x64",'--config',$Configuration,'--target','obs-frontend-api','--parallel','4')
Run cmake @('--install',"$obs/build_x64",'--config',$Configuration,'--component','Development','--prefix',$prefix)
} else {
  # Generate import libraries from the installed OBS DLL exports. Only the
  # matching headers are used from the existing source checkout; OBS is not built.
  New-Item -ItemType Directory -Force "$prefix/lib", "$prefix/include" | Out-Null
  foreach ($name in @('obs', 'obs-frontend-api')) {
    $dll = Join-Path $InstalledOBS "bin/64bit/$name.dll"
    if (!(Test-Path $dll)) { throw "Missing OBS DLL: $dll" }
    $exports = & dumpbin /nologo /exports $dll
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $dll" }
    $symbols = @($exports | ForEach-Object {
      if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] }
    })
    if (!$symbols.Count) { throw "No exports found in $dll" }
    @("LIBRARY $name.dll", 'EXPORTS') + $symbols | Set-Content "$prefix/lib/$name.def" -Encoding ascii
    Run lib @('/nologo', '/machine:x64', "/def:$prefix/lib/$name.def", "/out:$prefix/lib/$name.lib")
  }
  @('#pragma once', '#define OBS_RELEASE_CANDIDATE 0', '#define OBS_BETA 0') | Set-Content "$prefix/include/obsconfig.h" -Encoding ascii
}

# Additional Qt modules must use exactly the Qt shipped by the OBS SDK.
$qtVersionFile = Join-Path $qt 'lib/cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake'
if (!(Test-Path $qtVersionFile)) { $qtVersionFile = Join-Path $qt 'lib/cmake/Qt6Core/Qt6CoreConfigVersion.cmake' }
$qtVersion = [regex]::Match((Get-Content $qtVersionFile -Raw), 'set\(PACKAGE_VERSION\s+"?([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if (!$qtVersion) { throw 'Cannot determine OBS Qt version' }
foreach ($module in @(@('qtwebsockets','WebSockets'), @('qthttpserver','HttpServer'))) {
  $source = Join-Path $deps $module[0]
  if (!(Test-Path $source)) { Run git @('clone','--depth','1','--branch',"v$qtVersion","https://github.com/qt/$($module[0]).git",$source) }
  if (Test-Path "$qt/lib/cmake/Qt6$($module[1])/Qt6$($module[1])Config.cmake") { continue }
  Run cmake @('--fresh','-S',$source,'-B',"$source/build",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$qt", "-DCMAKE_INSTALL_PREFIX=$qt", '-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
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
$testSource = Join-Path $deps 'qtbase-test'
if (!(Test-Path $testSource)) {
  Run git @('clone','--depth','1','--filter=blob:none','--sparse','--branch',"v$qtVersion",'https://github.com/qt/qtbase.git',$testSource)
  Run git @('-C',$testSource,'sparse-checkout','set','src/testlib','LICENSES')
} elseif (!(Test-Path "$testSource/LICENSES")) {
  Run git @('-C',$testSource,'sparse-checkout','add','LICENSES')
}
if (!(Test-Path "$qt/lib/cmake/Qt6Test/Qt6TestConfig.cmake")) {
  Run cmake @('-S',"$repo/cmake/qttest",'-B',"$deps/qttest-build",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$qt", "-DCMAKE_INSTALL_PREFIX=$qt", "-DQTBASE_TEST_SOURCE=$testSource", '-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
  Run cmake @('--build',"$deps/qttest-build",'--parallel','4')
  Run cmake @('--install',"$deps/qttest-build")
}
$pluginArgs = @('-S',$repo,'-B',"$repo/build-windows",'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$prefixes",'-DBUILD_TESTING=ON')
if ($InstalledOBS) {
  $pluginArgs += @("-DOBS_INCLUDE=$obs/libobs", "-DOBS_FRONTEND_INCLUDE=$obs/frontend/api", "-DOBS_LIBRARY=$prefix/lib/obs.lib", "-DOBS_FRONTEND_LIBRARY=$prefix/lib/obs-frontend-api.lib", "-DCMAKE_CXX_FLAGS=/I`"$prefix/include`"")
}
Run cmake $pluginArgs
Run cmake @('--build',"$repo/build-windows",'--parallel','4')
$env:PATH = "$qt/bin;$prebuilt/bin;$prefix/bin;$env:PATH"
$zones = Get-ChildItem $prefix -Directory -Filter zoneinfo -Recurse | Select-Object -First 1
if (!$zones) { throw 'libical timezone data is missing' }
$env:BS_ZONEINFO = $zones.FullName
Run ctest @('--test-dir', "$repo/build-windows", '--output-on-failure', '-C', $Configuration)
$stage = Join-Path $repo "artifacts/windows-$version"
# Fresh staging prevents stale global OBS DLLs from entering a new package.
if (Test-Path $stage) {
  $stage = Join-Path $repo ("artifacts/windows-$version-" + [guid]::NewGuid().ToString('N'))
}
Run cmake @('--install',"$repo/build-windows",'--prefix',$stage)
Copy-Item -Recurse -Force $zones.FullName "$stage/broadcast-scheduler/data/"
New-Item -ItemType Directory -Force "$stage/broadcast-scheduler/bin/64bit" | Out-Null
foreach ($dll in @('Qt6HttpServer.dll','Qt6WebSockets.dll')) { Copy-Item "$qt/bin/$dll" "$stage/broadcast-scheduler/bin/64bit/" }
New-Item -ItemType Directory -Force "$stage/broadcast-scheduler/data/qt/tls" | Out-Null
Copy-Item "$qt/plugins/tls/qschannelbackend.dll" "$stage/broadcast-scheduler/data/qt/tls/"
Copy-Item "$repo/LICENSE", "$repo/THIRD_PARTY.md" "$stage/broadcast-scheduler/data/"
Copy-Item "$ical/LICENSE" "$stage/broadcast-scheduler/data/LICENSE-libical"
$notices = "$stage/broadcast-scheduler/data/licenses"
New-Item -ItemType Directory -Force $notices | Out-Null
Copy-Item "$repo/licenses/*" $notices
Copy-Item "$ical/COPYING" "$notices/libical-COPYING.txt"
foreach ($moduleName in @('qthttpserver', 'qtwebsockets')) {
  New-Item -ItemType Directory -Force "$notices/$moduleName" | Out-Null
  Copy-Item "$deps/$moduleName/LICENSES/*" "$notices/$moduleName/"
}
New-Item -ItemType Directory -Force "$notices/qtbase" | Out-Null
Copy-Item "$testSource/LICENSES/*" "$notices/qtbase/"
Copy-Item "$repo/docs/source-distribution.md" "$stage/broadcast-scheduler/data/SOURCES.md"
if ($InstalledOBS) { Run python @("$repo/tests/windows_loader.py", $stage, $InstalledOBS, "$repo/build-windows/package-probe.exe") }
Compress-Archive -Force "$stage/*" "$repo/artifacts/broadcast-scheduler-$version-windows-x64.zip"
if (!$SkipInstaller) {
  $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if (!$iscc) {
    throw 'Inno Setup 6 is required to create the user-friendly installer. Install it or pass -SkipInstaller.'
  }
  Run $iscc.Source @('/Qp', "/DStageDir=$stage", "/DProductVersion=$version", (Join-Path $repo 'installer/BroadcastScheduler.iss'))
  Write-Host "Installer ready: artifacts/Broadcast-Scheduler-$version-Setup.exe"
}
Write-Host "Package ready: artifacts/broadcast-scheduler-$version-windows-x64.zip (Qt $qtVersion)"

Run python @("$repo/scripts/release-audit.py", '--package', "$repo/artifacts/broadcast-scheduler-$version-windows-x64.zip")
$packages = @("$repo/artifacts/broadcast-scheduler-$version-windows-x64.zip")
if (!$SkipInstaller) { $packages += "$repo/artifacts/Broadcast-Scheduler-$version-Setup.exe" }
$packages | ForEach-Object { $h = Get-FileHash -LiteralPath $_ -Algorithm SHA256; "$($h.Hash.ToLower())  $([IO.Path]::GetFileName($_))" } | Set-Content "$repo/artifacts/SHA256SUMS-$version.txt" -Encoding ascii
