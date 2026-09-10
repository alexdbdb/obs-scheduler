# Broadcast Scheduler

[![Build and test](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml/badge.svg)](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml)

Schedule the start and end of OBS recordings from manual events, recurring events, ICS calendars, Google Calendar, Odoo Events or a local API. External credentials are supplied locally by each user.

**Version 0.2.2.** Target: Windows x64, OBS 32.2.2 / Qt 6.11.1. Google and Odoo synchronization have been confirmed by a user; unattended operation under failure conditions still needs qualification. See [validation](docs/validation.md).

## Download

[**Download the Windows installer (.exe)**](https://github.com/alexdbdb/obs-scheduler/releases/download/v0.2.2/Broadcast-Scheduler-0.2.2-Setup.exe) · [View all release files](https://github.com/alexdbdb/obs-scheduler/releases)

Alternatively, build it locally.

Close OBS, run the installer, and reopen OBS. No source-code download or compilation is needed. See [release notes](CHANGELOG.md) for compatibility and limitations.

## Language

The interface follows OBS's language setting: English by default, Spanish when OBS uses Spanish. Settings, provider error explanations, History and Logs are localized; technical details returned by external services retain their original language. Documentation is maintained in English, with a Spanish Google setup guide.

## Scope

- Manual schedules with a title, start, end and enabled state.
- Daily, weekly, weekday and monthly recurrences.
- HTTPS ICS subscriptions and local ICS files.
- Google account connection in the system browser, followed by calendar selection.
- Direct, read-only import of Odoo `event.event`, with type, stage and state filters (Odoo 16+).
- Local REST API for event CRUD, status and synchronization.
- Agenda, day/week/month filters, execution history and logs.

The plugin uses the device timezone for manual times and display. It does not provide recording buttons, templates, streaming controls or a timezone selector. OBS continues to control the recording format, encoder, destination and sources.

## Windows installation

Builds target **OBS 32.2.2 x64 with Qt 6.11.1**. Other OBS/Qt combinations are not validated.

1. Download the [0.2.2 Windows installer](https://github.com/alexdbdb/obs-scheduler/releases/download/v0.2.2/Broadcast-Scheduler-0.2.2-Setup.exe), or build it locally.
2. Close OBS before installing.
3. Run `Broadcast-Scheduler-0.2.2-Setup.exe`. It installs under `C:\ProgramData\obs-studio\plugins\broadcast-scheduler`.
4. Open **Docks → Broadcast Scheduler**.

For developers, a portable ZIP is also available in successful [Actions builds](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml). Extract its `broadcast-scheduler` folder into `C:\ProgramData\obs-studio\plugins`, preserving its paths. The package keeps its Qt dependencies inside the plugin folder. Portable OBS installations require the manual path described in the [OBS plugins guide](https://obsproject.com/kb/plugins-guide). Uninstalling the plugin retains its database and settings.

Google and Odoo credentials are never bundled into the plugin. To use Google Calendar, create a Desktop OAuth client in your own Google Cloud project and enter its Client ID and client secret in **Settings → Google**. To use Odoo, enter your server and a user API key in **Settings → Odoo**. Secrets use OS-protected credential storage, not the database. Follow the [Google setup guide](docs/google-setup.md) ([Español](docs/registro-google.md)) or the [Odoo setup guide](docs/odoo.md).

## First recording

Configure a writable recording destination in OBS. Click **Schedule Recording**, choose a start two minutes ahead and an end two minutes later, and save. Ensure the scheduler is enabled. Check the recording and the two transitions in History.

A recording started manually in OBS is left untouched. Scheduled recordings use `YYYY-MM-DD - Event name` as the filename while retaining OBS's configured folder and extension. Invalid filename characters are replaced. Overlapping or adjacent scheduled events share a recording and retain the name of the event that started it; they do not necessarily produce separate files. A start more than 60 seconds late is skipped, and a finished event is never started late. Pausing prevents new starts but preserves stops already scheduled for recordings in progress.

Each enabled event records from its start to its end.

## Calendars and API

Add an HTTPS ICS URL or a local ICS file in **Calendars**, or configure Google/Odoo from **Settings**. Google and Odoo synchronize automatically when OBS opens and then at each configured refresh interval, keeping a rolling seven-day window (including events already in progress). External events cannot be edited in the agenda, but deleting one hides it locally across future synchronizations without deleting it from the source. Use Ctrl+click to select multiple upcoming events and Delete or the Delete key to remove them together. Removing and re-adding the calendar clears these local exclusions. Recurrences are managed separately.

The API is disabled by default and binds to `127.0.0.1`. Generate a token in Settings → API before enabling it. See [API examples](docs/api.md) and [OpenAPI](docs/openapi.json). Only loopback connections are supported; non-local addresses are rejected.

## Development and limitations

See [build instructions](docs/build.md), [architecture](docs/architecture.md), [Google integration](docs/google-calendar.md), [Odoo integration](docs/odoo.md) and [validation](docs/validation.md). The project uses C++17, Qt, SQLite and libical. Windows and Linux builds are checked in GitHub Actions; macOS packaging is not validated.

OBS must remain running and the computer awake. There is no automatic wake-up. Encoder failures, storage exhaustion, sleep/resume, ambiguous clock changes and long unattended sessions need further testing. History and logs have no automatic retention policy.

Report reproducible problems in [Issues](https://github.com/alexdbdb/obs-scheduler/issues), including OBS/OS versions and sanitized logs. Do not attach tokens, private calendar URLs or personal recordings. See [contribution guidance](CONTRIBUTING.md) and [security reporting](SECURITY.md).

## License

Maintained by [alexdbdb](https://github.com/alexdbdb) and contributors. Current project source is **GPL-3.0-or-later**. The combined binary is distributed under **GPLv3**, including Qt HTTP Server under its applicable GPLv3 terms. The installer displays GPLv3 and bundles the applicable notices and license texts. See [LICENSE](LICENSE), [GPLv3](licenses/GPL-3.0.txt) and [dependency inventory](THIRD_PARTY.md).
