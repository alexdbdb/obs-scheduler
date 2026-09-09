# Source and notices for distributed builds

Project source and build scripts: https://github.com/alexdbdb/obs-scheduler

The public repository contains original source under GPL-2.0-or-later. The combined open-source plugin binary is distributed under GPLv3; the installer displays that text. The package includes original-project, Qt and libical notices under its data directory. This does not change the license of upstream source files.

Windows dependency sources used by the build:

- OBS headers/development libraries: https://github.com/obsproject/obs-studio/tree/32.2.2
- Qt HTTP Server: https://github.com/qt/qthttpserver/tree/v6.11.1
- Qt WebSockets: https://github.com/qt/qtwebsockets/tree/v6.11.1
- Qt Test (tests only, not shipped): https://github.com/qt/qtbase/tree/v6.11.1/src/testlib
- libical: https://github.com/libical/libical/tree/v3.0.20
- SQLite amalgamation: https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip

Qt Core/Gui/Widgets and OBS binaries are supplied by the user's OBS installation, not included in the plugin installer. The build reads SDK archive hashes and versions from the pinned OBS metadata.

When publishing a binary release, identify the exact project commit and provide the corresponding source, dependency versions and build instructions alongside it. Preserve all applicable upstream copyright/license notices. Do not include personal OAuth tokens or local OBS configuration in source archives. CI artifacts are preliminary builds, not a claim of stable release qualification.
