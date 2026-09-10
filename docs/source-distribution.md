# Source and notices for distributed builds

Project source and build scripts: https://github.com/alexdbdb/obs-scheduler

The public repository contains current source under GPL-3.0-or-later. The combined open-source plugin binary is distributed under GPLv3; the installer displays that text. The package includes original-project, Qt and libical notices under its data directory. This does not change the license of upstream source files.

Windows dependency sources used by the build:

- OBS headers/development libraries: https://github.com/obsproject/obs-studio/tree/32.2.2
- Qt HTTP Server: https://github.com/qt/qthttpserver/tree/v6.11.1
- Qt WebSockets: https://github.com/qt/qtwebsockets/tree/v6.11.1
- Qt Test and Qt Network Schannel TLS backend: https://github.com/qt/qtbase/tree/v6.11.1
- libical: https://github.com/libical/libical/tree/v3.0.20
- SQLite amalgamation: https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip

Qt Core/Gui/Widgets and OBS binaries are supplied by the user's OBS installation, not included in the plugin installer. The installer includes the matching Qt Network Schannel backend in the plugin's own data/qt/tls directory because some OBS installations do not provide a functional Qt TLS plugin. The build reads SDK archive hashes and versions from the pinned OBS metadata.

When publishing a binary release, identify the exact project commit and provide the corresponding source, dependency versions and build instructions alongside it. Preserve all applicable upstream copyright/license notices. Do not include personal OAuth tokens or local OBS configuration in source archives. CI artifacts are preliminary builds, not a claim of stable release qualification.

After committing a reviewed build, run `python scripts/package-sources.py`. It requires a clean worktree and creates a separate source ZIP containing the exact project commit, Qt HTTP Server/WebSockets/Qt Base sources, libical and the SQLite amalgamation. A manifest records dependency commits and SHA-256 hashes. The Qt Base source download can be substantial; it is cached under the ignored .deps directory.

Tagged Windows CI builds generate this source bundle and include it in the draft release alongside the installer, plugin ZIP and a SHA256SUMS file covering all three assets. The source bundle is deliberately separate from the small runtime plugin package.
