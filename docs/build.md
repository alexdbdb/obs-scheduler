# Building and installing

## Windows 10/11 x64, OBS 32.x

Install Visual Studio 2022 with Desktop development with C++, Windows 10/11 SDK, CMake 3.30+, Ninja, Git, PowerShell 7, Perl (required by libical's code generation) and Inno Setup 6. Open **Developer PowerShell for VS 2022** with x64 tools. Run:

```powershell
git clone <YOUR_REPOSITORY_URL>/obs-broadcast-scheduler.git
cd obs-broadcast-scheduler
pwsh -File scripts/build-windows.ps1
```

The script pins OBS 32.2.2 as the 32.x baseline, downloads hash-verified official OBS/Qt dependency archives, builds libobs/frontend SDK with the same pattern as OBS's official plugin template, builds libical/SQLite and any missing Qt HTTP modules, compiles the plugin and assembles both `artifacts/broadcast-scheduler-0.1.0-windows-x64.zip` and the user-friendly `artifacts/Broadcast-Scheduler-0.1.0-Setup.exe`. The official OBS Qt SDK archive does not include Qt6Test, so the Windows packaging build disables the QtTest target; the full scheduler/API test suite runs in the Linux CI job. No full OBS executable build is required for the SDK. Internet access and several GB of disk space are required. CI installs Inno Setup and performs these steps on windows-2022. This Linux development environment cannot locally execute that Windows toolchain; CI must pass before publishing a Windows binary.

To build only the portable ZIP when Inno Setup is unavailable, pass `-SkipInstaller` to the script. The installer source is [installer/BroadcastScheduler.iss](../installer/BroadcastScheduler.iss); it detects common OBS install locations, validates `obs64.exe`, copies the plugin/runtime files and leaves the user database intact during uninstall.

Close OBS. Extract the ZIP into the **OBS installation root**, normally `C:\Program Files\obs-studio`, preserving paths:

```text
obs-studio/
  obs-plugins/64bit/broadcast-scheduler.dll
  data/obs-plugins/broadcast-scheduler/locale/en-US.ini
  data/obs-plugins/broadcast-scheduler/locale/es-ES.ini
  data/obs-plugins/broadcast-scheduler/zoneinfo/...
  bin/64bit/Qt6HttpServer.dll
  bin/64bit/Qt6WebSockets.dll
```

The extra Qt modules must match OBS's Qt build. Do **not** overwrite Qt Core/Gui/Widgets or libobs with SDK copies. When OBS upgrades its Qt ABI, rebuild against that OBS SDK. Restart OBS, enable **Docks → Broadcast Scheduler**, inspect Help → Log Files → View Current Log for `[broadcast-scheduler]`. Uninstall by removing only these plugin-specific files; keep the user database unless deliberately retiring its history. User state normally lives at `%APPDATA%\obs-studio\plugin_config\broadcast-scheduler\scheduler.sqlite3`; the authoritative path is shown in Settings → Advanced.

## Reproducible Linux development

```sh
docker build -f Dockerfile.dev -t broadcast-scheduler-dev .
docker run --rm -v "$PWD:/work" broadcast-scheduler-dev bash -lc \
  'cmake -S . -B build -G Ninja -DOBS_SOURCE_DIR=/opt/obs-src -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j2 && ctest --test-dir build --output-on-failure'
```

The container uses the official OBS PPA for Ubuntu 24.04. Its obs-studio package includes development files; installing Ubuntu's separate libobs-dev causes a package conflict and is intentionally avoided. It also installs Qt 6, SQLite, libical, libsecret, SIMDe, Xvfb and compiler tools. It does not mount host service configuration or the Docker socket.

For a system install in that environment: `cmake --install build --prefix /usr` (administrative permissions required). For a user's OBS plugin directory, place `.so` under `~/.config/obs-studio/plugins/broadcast-scheduler/bin/64bit/` and `data/*` under its `data/` directory. A GUI or virtual X server and OpenGL/Mesa are needed to run OBS.

## macOS

Core code includes Keychain and portable Qt/SQLite/libical paths. Native macOS packaging/signing and execution are not validated here. Build with an OBS development SDK, matching Qt with HttpServer/WebSockets, SQLite and libical, then configure CMake with those prefixes. Distribution needs proper OBS `.plugin` bundle metadata, install names, universal architectures if desired, and signing/notarization. Windows is the initial supported packaging target; do not treat the macOS CMake branch as a release-ready package.

## Tests and debugging

`ctest --test-dir build --output-on-failure` runs QtTest scenarios. `scheduler-service <db-path>` starts the actual store/scheduler/API without OBS; its action adapter explicitly returns a failure, not simulated recording success. `BS_TEST_TOKEN` enables its API for integration tests only. Production plugin never reads that environment variable. `BS_ZONEINFO` selects bundled timezone files for Windows core tests. Format using `.clang-format`. Build with AddressSanitizer/UBSan where supported for further qualification. See [validation](validation.md) for actual results and gaps.
