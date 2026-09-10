# Changelog

## 0.2.0-beta.1 — public beta candidate

- Google Calendar with each user's own Desktop OAuth client and protected client-secret/token storage.
- Direct Odoo Events import with event-type, stage and state filters.
- Automatic Google/Odoo refresh at OBS startup and at the configured interval, using a rolling seven-day window.
- Persistent local exclusion of imported events, multiple selection and Delete-key support.
- Scheduled recording filenames use YYYY-MM-DD - Event name.
- English default and Spanish localization, including scheduler diagnostics and provider error explanations.
- Scrollable settings pages and a bilingual Google setup guide.
- Localhost-only authenticated API; earlier network bindings migrate to localhost.
- Windows per-plugin installation under ProgramData, with Qt modules and TLS backend inside the plugin directory.
- GPL-3.0-or-later for current project source, dependency notices, credential-pattern checks and package checksums.

Compatibility target: **Windows x64, OBS 32.2.2, Qt 6.11.1**. Other OBS/Qt combinations are not yet qualified. Linux builds are development checks; macOS is unvalidated.

### Upgrade from 0.1.0

Close OBS and uninstall the old plugin using Windows Settings before installing 0.2. The new installer refuses to proceed while the old installer registration exists, preventing two copies from loading. The database is retained. Enter your own Google client credentials and reconnect if necessary. The API is now local-only.

For a manually installed old ZIP, remove only the old obs-plugins/64bit/broadcast-scheduler.dll and data/obs-plugins/broadcast-scheduler from your OBS application directory after backing them up. Do not delete shared Qt DLLs from OBS: another component may use them.

## 0.1.0 — historical prerelease

Initial recording scheduler and experimental Google integration. Superseded; do not use its installer for public distribution.
