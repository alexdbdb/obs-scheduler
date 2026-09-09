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
- `artifacts/Broadcast-Scheduler-0.1.0-Setup.exe`
- `artifacts/broadcast-scheduler-0.1.0-windows-x64.zip`

Internet access and several GB of free space are needed on the first build. Build caches and downloads live in `.deps` and are ignored by Git. Use `-SkipInstaller` for ZIP-only packaging.

## Windows: use an existing OBS installation

When matching headers are already cached in `.deps/obs-studio-32.2.2`, the helper initializes VS 2022 and generates import libraries from installed OBS DLLs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installed-obs.ps1
```

The default installation path is `C:\Program Files\obs-studio`; override it with `-OBSPath`. This path was tested with OBS 32.2.2 and Qt 6.11.1. The helper does not rebuild OBS. Other dependencies still have to be available or downloaded. If the matching header checkout is absent, the underlying script obtains it.

To enable Google for a distribution, pass the downloaded Desktop client JSON:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installed-obs.ps1 -GoogleClientFile .deps/google-client.json
```

The clean-build script accepts the same `-GoogleClientFile` option. CMake users can set `GOOGLE_OAUTH_CLIENT_FILE`. Builds without it omit the bundled client and explain that Google is not configured. Never commit credential JSON or account tokens. See the [registration guide](registro-google.md).

## Package contents

Close OBS before installing. The ZIP preserves these paths:

```text
obs-plugins/64bit/broadcast-scheduler.dll
data/obs-plugins/broadcast-scheduler/locale/...
data/obs-plugins/broadcast-scheduler/zoneinfo/...
data/obs-plugins/broadcast-scheduler/licenses/...
bin/64bit/Qt6HttpServer.dll
bin/64bit/Qt6WebSockets.dll
```

Do not replace Qt Core/Gui/Widgets with SDK copies. Rebuild when OBS changes its Qt ABI. The installer retains the user database, normally under `%APPDATA%/obs-studio/plugin_config/broadcast-scheduler`; Settings → Diagnostics shows the actual location.

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

`ctest --test-dir build --output-on-failure` runs the core and HTTP tests. Windows builds also test OAuth loopback handling and DPAPI in temporary directories. See [validation](validation.md) for evidence and remaining gaps.
