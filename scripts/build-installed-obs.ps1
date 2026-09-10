param([string]$OBSPath = 'C:\Program Files\obs-studio')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ Build Tools are required' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$environment = & cmd /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the C++ compiler environment' }
foreach ($line in $environment) {
  if ($line -match '^([^=]+)=(.*)$') {
    [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
  }
}
$cmakeTools = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake'
$env:VSLANG = '1033'
$env:Path = "$repo/.deps/build-tools/cmake/data/bin;$cmakeTools/CMake/bin;$cmakeTools/Ninja;${env:ProgramFiles}/Inno Setup 6;${env:ProgramFiles(x86)}/Inno Setup 6;${env:LOCALAPPDATA}/Programs/Inno Setup 6;$env:Path;C:/Program Files/Git/usr/bin"
& "$PSScriptRoot/build-windows.ps1" -InstalledOBS $OBSPath
exit 0
