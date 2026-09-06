# Licensing and dependency inventory

Original Broadcast Scheduler source: **GPL-2.0-or-later**. Copyright 2026 Broadcast Scheduler contributors. Author metadata intentionally contains `AUTHOR_NAME (replace before publishing)`.

| Component | License selected | Purpose |
| --- | --- | --- |
| OBS libobs / frontend API | GPL-2.0-or-later | Native integration; supplied by OBS |
| Qt Core, Widgets, Network, Concurrent, Test, WebSockets | GPL-3.0 distribution option (also offered under LGPL/commercial terms) | UI, event loops, networking and tests |
| Qt HTTP Server | GPL-3.0 | Mature HTTP parser and routing |
| libical 3.0.x (Windows pinned 3.0.20) | LGPL-2.1, using its GPL conversion permission | RFC 5545 parsing, recurrence and timezone database |
| SQLite (Windows 3.50.4) | Public domain | Transactional persistence |
| libsecret / GLib (Linux only) | LGPL-2.1-or-later | Desktop Secret Service |
| Windows Crypt32; macOS Security framework | OS system libraries | Protected credentials |
| CMake, Ninja, compiler, Git, Docker, Xvfb | Build/test tools; not linked into plugin | Reproducible builds |

**The linked open-source binary must be distributed under GPLv3**, exercising the “or later” permission of this project's source and OBS. Qt HTTP Server is GPLv3-only, not LGPL. Do not describe the combined binary as GPLv2-only. See [Qt HTTP Server licensing](https://doc.qt.io/qt-6/qthttpserver-index.html#licenses), [libical license](https://github.com/libical/libical/blob/v3.0.20/LICENSE), [GPLv2](LICENSE) and [GPLv3](licenses/GPL-3.0.txt).

No proprietary Google SDK is linked: Google integration uses documented HTTPS APIs. OAuth credentials are provided by the developer, never shipped as hardcoded secrets.

Before distributing binaries, include the corresponding source (this repository, exact dependency sources and build scripts), copyright notices and applicable license texts. Do not ship unrelated OBS SDK DLLs or replace OBS's Qt Core/Gui/Widgets DLLs. The Windows package adds only the two extra Qt modules, compiled against OBS's Qt version. Dependency archives remain available in `.deps` for source-compliance packaging. `scripts/build-windows.ps1` verifies official OBS dependency SHA-256 hashes from the pinned OBS buildspec. Libical and Qt source tags are pinned; SQLite's source archive version is pinned.
