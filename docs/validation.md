# Validation and release status

Version 0.2.2 is a public maintenance release. Evidence below describes local Windows checks on 10 September 2026.

## Verified locally

- Visual Studio 2022 build, Qt 6.11.1 and OBS 32.2.2 headers, linked against installed OBS DLLs.
- QtTest: 27 passed, none failed or skipped (including setup/cleanup). Coverage includes scheduler/calendar behavior, event-based filenames, the local OAuth handshake, protected Odoo configuration, localhost-only API policy and parameterized diagnostic translation.
- HTTP API: five tests passed, covering authentication, CRUD, invalid input, sync and device-local timestamps.
- The package loaded through OBS's own DLL loader against an isolated OBS runtime, excluding previously installed global Qt HTTP/WebSockets DLLs. The plugin-local Schannel TLS backend initialized successfully.
- OBS's native locale parser verified every entry in both English and Spanish. A separate check verifies catalog parity, unique keys and matching placeholders.
- Real OBS 32.2.2 in two disposable portable profiles (English and Spanish) started and stopped a scheduled recording, confirmed both transitions in History and produced an MKV named with the event's local date and title.
- A test installer with a separate AppId and user privileges installed all staged files with matching hashes and uninstalled the plugin successfully. The production admin/UAC flow and migration from a legacy installation remain manual checks.
- A redacted scan of working files and reachable Git history found none of the checked credential formats or private/generated tracked paths. When the private Google JSON is present, its exact Client ID and secret are also compared without being printed.
- In preceding development builds, the user confirmed Google authorization/calendar synchronization with their own client and live Odoo event synchronization. This does not qualify every Odoo version or server configuration.

The OAuth test covers Client ID validation, authorization URL, PKCE, repeated clicks, state rejection, denial, retry and cancellation with a loopback socket and synthetic credentials. It does not contact Google or package real credentials.

## Still to qualify

- Fresh real-account Google/Odoo verification from the final release installer. Each Google project is user-owned; no shared developer client is submitted for verification.
- Both Odoo protocols against separate real 16–19 servers, startup refresh while offline and reconnection after token expiry.
- Visual inspection, screenshots, high-DPI layout and keyboard accessibility of the final UI. The Windows Computer Use service was unavailable; native locale parsing and real OBS execution do not replace a visual check.
- Production installer elevation, legacy-install migration and reboot/startup behavior in a clean Windows VM.
- Long unattended sessions, disk-full/encoder errors, delayed OBS failures, sleep/resume and all daylight-saving edge cases.
- Current Linux runtime/package installation; its CI build is a development check.
- macOS builds and packaging.

## Automated workflows

CI builds Windows and Linux from fresh checkouts and runs core, locale and API tests. Windows-only OAuth/DPAPI tests skip on Linux. The optional Linux OBS smoke test needs a disposable container, Xvfb, ImageMagick and ffprobe. Windows has a separate portable-profile recording test and a user-mode installer test. See [build instructions](build.md).

Tagged builds create a draft prerelease after CI passes. Follow [the release process](releasing.md), including retiring the old Google client and historical assets before public launch. GitHub CLI authentication was unavailable during local preparation, so those remote operations are not recorded as completed.

Historical audit/build notes are not current release guarantees. See [Actions](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml) for the result of each pushed commit.
