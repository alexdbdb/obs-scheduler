# Validation record

Validation is performed in the Linux Docker development image against OBS 32.2.0, Qt 6.4.2, SQLite 3.45, libical from Ubuntu 24.04 and the native plugin ABI.

The last completed automated result is:

```text
Test project /work/build
1/2 Test #1: scheduler-tests ..................   Passed
2/2 Test #2: api-integration ..................   Passed
100% tests passed, 0 tests failed
```

The QtTest suite reports 17 passing scenarios covering the scheduler and calendar cases requested. The API integration test passes CRUD, authentication, CORS rejection, validation, template listing and sync request behavior.

OBS startup was also verified: the module appears in the OBS loaded module list, OBS 32.2.0 launches under Xvfb, the dock is registered and the scheduler executes a real recording start/stop. The recording part created and probed an MKV and recorded confirmed execution history. The streaming part reached OBS's native stream-start path, but this container has no working local RTMP listener; OBS therefore cannot confirm the output and the smoke test stops before declaring streaming success. The test keeps the action visible as `requested`/`indeterminate` instead of reporting a false success. `tests/obs_smoke.py` is ready to rerun with an OBS stream service and RTMP endpoint:

```sh
cmake --build build -j2
python3 tests/obs_smoke.py
```

The Windows build and the Google Calendar OAuth flow cannot be fully validated on this Linux host. Windows uses the pinned official OBS 32.2.2 SDK/dependency workflow in CI, matching the current OBS 32.2.x Qt runtime. Google requires a developer-owned Desktop OAuth client, consent screen/test user and browser authorization; no credentials are present in this repository. Production qualification must additionally cover encoder/device failures, sleep/wake, system clock changes, ambiguous DST folds and unattended recovery.
