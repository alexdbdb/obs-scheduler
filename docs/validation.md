# Validation and release status

Version 0.1.0 is a prerelease. Test counts below describe the local Windows checks performed on 8 September 2026; the [Actions page](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml) provides results for each published commit.

## Verified locally

- Compiled with Visual Studio 2022, Qt 6.11.1 and OBS 32.2.2 headers, linking against the installed OBS DLLs.
- QtTest: 21 passed, none failed or skipped on Windows. This includes scheduler/calendar behavior and the local Google authorization handshake.
- HTTP API: five tests passed, including authentication, CRUD, invalid input, sync and device-local timestamps.
- The packaged DLL loaded successfully against the installed OBS 32 runtime.
- A user confirmed scheduled recording starts and stops correctly in OBS after installing the recording-only build.

The OAuth test checks the authorization URL, PKCE challenge, repeated clicks, state rejection, denial, retry and cancellation using loopback sockets and temporary credentials. It does not contact Google. The build configuration also accepted a synthetic Desktop client JSON and rejected a Web client JSON in an isolated configure-only check. Synthetic credentials were not packaged.

## Not yet verified

- Authorization, refresh and synchronization with a real Google account. A registered client and user consent are required.
- Google verification for public distribution.
- A complete interactive test of the new Google settings panel.
- Long unattended sessions, disk-full/encoder errors, delayed OBS failures, sleep/resume and all daylight-saving edge cases.
- macOS builds and packaging.

## Automated workflows

CI builds Windows and Linux from fresh checkouts and runs the core and API tests. The Windows-only OAuth/DPAPI test is explicitly skipped on Linux. The separate Linux OBS smoke script requires an isolated container, an installed plugin, Xvfb, ImageMagick and ffprobe; it is not part of the default CI test count.

Old local audit and build notes are historical evidence, not current release guarantees. Do not infer Google connectivity or production readiness from a successful unit-test run.
