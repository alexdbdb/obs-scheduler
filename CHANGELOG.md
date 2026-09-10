# Changelog

## 0.2.1 — maintenance release

- Removed the redundant status dashboard from the main dock to keep the scheduler compact.
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
