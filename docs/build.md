# Building and installing

## Windows: clean build

Requirements: Visual Studio 2022 C++ x64 tools, Windows SDK, CMake 3.30 or newer, Ninja, Git, Perl, Python 3, PowerShell 7 and Inno Setup 6. Open an x64 Developer PowerShell:

```powershell
git clone https://github.com/alexdbdb/obs-scheduler.git
cd obs-scheduler
pwsh -File scripts/build-windows.ps1
```

The script pins OBS 32.2.2, reads its dependency metadata and verifies the official prebuilt and Qt SDK archive hashes. It builds the OBS development libraries (not the OBS application), missing matching Qt HTTP/WebSockets/Test modules, libical 3.0.20 and SQLite 3.50.4. The plugin and tests are then built; packaging runs only after tests pass.

Artifacts:

- `artifacts/Broadcast-Scheduler-0.2.0-beta.1-Setup.exe`
- `artifacts/broadcast-scheduler-0.2.0-beta.1-windows-x64.zip`
- `artifacts/SHA256SUMS-0.2.0-beta.1.txt`

Internet access and several GB of free space are needed on the first build. Build caches and downloads live in `.deps` and are ignored by Git. Use `-SkipInstaller` for ZIP-only packaging.

## Windows: use an existing OBS installation

When matching headers are already cached in `.deps/obs-studio-32.2.2`, the helper initializes VS 2022 and generates import libraries from installed OBS DLLs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installed-obs.ps1
```

The default installation path is `C:\Program Files\obs-studio`; override it with `-OBSPath`. This path was tested with OBS 32.2.2 and Qt 6.11.1. The helper does not rebuild OBS. Other dependencies still have to be available or downloaded. If the matching header checkout is absent, the underlying script obtains it.

Google and Odoo credentials are not build inputs and are never bundled into a distribution. Each user enters them after installation. Never commit downloaded credential JSON, API keys or account tokens. See the [Google registration guide](google-setup.md) and [Odoo setup](odoo.md).

## Package contents

Close OBS before installing. The ZIP preserves these paths:

```text
broadcast-scheduler/bin/64bit/broadcast-scheduler.dll
broadcast-scheduler/data/locale/...
broadcast-scheduler/data/zoneinfo/...
broadcast-scheduler/data/licenses/...
broadcast-scheduler/bin/64bit/Qt6HttpServer.dll
broadcast-scheduler/bin/64bit/Qt6WebSockets.dll
broadcast-scheduler/data/qt/tls/qschannelbackend.dll
```

Extract the ZIP into `C:\ProgramData\obs-studio\plugins`. The installer uses this same per-plugin layout. Uninstall the legacy 0.1.0 plugin before upgrading; do not install two copies.

The Schannel plugin supplies HTTPS through the Windows certificate store when OBS does not bundle a Qt TLS backend. Do not replace Qt Core/Gui/Widgets with SDK copies. Rebuild when OBS changes its Qt ABI. The two extra Qt DLLs sit beside the plugin DLL; OBS loads them through its module-local DLL search path. The plugin registers its own data/qt directory for TLS discovery, leaving OBS's application directory untouched. The installer retains the user database, normally under `%APPDATA%/obs-studio/plugin_config/broadcast-scheduler`; Settings → Diagnostics shows the actual location.

## Linux

```sh
docker build -f Dockerfile.dev -t broadcast-scheduler-dev .
docker run --rm -v "$PWD:/work" broadcast-scheduler-dev bash -lc \
  'cmake -S . -B build -G Ninja -DOBS_SOURCE_DIR=/opt/obs-src -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build --output-on-failure'
```

The image uses Ubuntu 24.04, the OBS PPA, Qt, SQLite, libical and libsecret. External repositories can change over time; the CI log is the record of the dependency versions used by that run.

For the optional real-OBS smoke test, install the plugin inside the disposable container with `cmake --install build`, then run `python3 tests/obs_smoke.py`. Never run that script against a personal OBS profile.

## macOS and tests

macOS-specific credential storage exists, but its build, signed bundle and runtime are not validated.

## Additional Windows qualification

With a package staging directory available:

```powershell
python tests/windows_loader.py artifacts/windows-0.2.0-beta.1 "C:/Program Files/obs-studio" build-windows/package-probe.exe
python tests/windows_obs_smoke.py artifacts/windows-0.2.0-beta.1
python tests/windows_obs_smoke.py artifacts/windows-0.2.0-beta.1 --language es-ES
pwsh -File tests/installer_smoke.ps1 -Stage artifacts/windows-0.2.0-beta.1
python scripts/release-audit.py --history
```

Repeated builds use a fresh staging directory with a unique suffix; substitute the generated directory in these commands. The real-OBS test copies application files into a disposable portable profile under artifacts, records synthetic events and retains its evidence there. It does not use personal calendars or OBS settings. The installer test uses a different AppId and user privileges; its InstallerTest executable must not be distributed as the release installer.

`ctest --test-dir build --output-on-failure` runs the core and HTTP tests. Windows builds also test OAuth loopback handling and DPAPI in temporary directories. See [validation](validation.md) for evidence and remaining gaps.
