param([Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$stagePath = (Resolve-Path -LiteralPath $Stage).Path
$iscc = @("${env:LOCALAPPDATA}/Programs/Inno Setup 6/ISCC.exe",
          "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
          "${env:ProgramFiles}/Inno Setup 6/ISCC.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$iscc) { throw 'Inno Setup not found' }
# Test-only AppId and user privileges keep the real installation untouched.
& $iscc /Qp "/DStageDir=$stagePath" /DPackageTest "$repo/installer/BroadcastScheduler.iss"
if ($LASTEXITCODE -ne 0) { throw 'Test installer compilation failed' }
$testRoot = Join-Path $repo ("artifacts/installer-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$target = Join-Path $testRoot 'broadcast-scheduler'
$setup = Join-Path $repo 'artifacts/Broadcast-Scheduler-0.2.1-InstallerTest.exe'
$arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=`"$target`"", "/LOG=`"$testRoot/install.log`"")
$process = Start-Process -FilePath $setup -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) { throw "Installer exited $($process.ExitCode)" }
Get-ChildItem -LiteralPath "$stagePath/broadcast-scheduler" -File -Recurse | ForEach-Object {
  $relative = [IO.Path]::GetRelativePath("$stagePath/broadcast-scheduler", $_.FullName)
  $installed = Join-Path $target $relative
  if (!(Test-Path -LiteralPath $installed) -or
      (Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) {
    throw "Installed file mismatch: $relative"
  }
}
$uninstaller = Join-Path $target 'uninstall/unins000.exe'
$process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$testRoot/uninstall.log`"") -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) { throw "Uninstaller exited $($process.ExitCode)" }
if (Test-Path -LiteralPath "$target/bin/64bit/broadcast-scheduler.dll") { throw 'Plugin DLL survived uninstall' }
Write-Output "PASS: installer file hashes and uninstall in isolated user-mode test. Logs: $testRoot"
