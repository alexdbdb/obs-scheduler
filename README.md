# Broadcast Scheduler

[![Build and test](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml/badge.svg)](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml)

Schedule the start and end of OBS recordings from manual events, recurring events, ICS calendars or a local API. Google Calendar integration is implemented and requires a distributor-configured OAuth client.

**Prerelease 0.1.0.** Windows recording start/stop has been tested locally and confirmed by a user. Google authorization with a real account and unattended operation under failure conditions remain to be qualified. See [validation](docs/validation.md).

## Download / Descargar

**[Download Windows installer (.exe) / Descargar instalador para Windows](https://github.com/alexdbdb/obs-scheduler/releases/download/v0.1.0/Broadcast-Scheduler-0.1.0-Setup.exe)**

Close OBS, run the installer, and reopen OBS. No source-code download or compilation is needed. See [release notes](https://github.com/alexdbdb/obs-scheduler/releases/tag/v0.1.0) for compatibility and limitations. While the repository is private, downloads require signing in with an account that has access.

## Scope

- Manual schedules with a title, start, end and enabled state.
- Daily, weekly, weekday and monthly recurrences.
- HTTPS ICS subscriptions and local ICS files.
- Google account connection in the system browser, followed by calendar selection.
- Local REST API for event CRUD, status and synchronization.
- Agenda, day/week/month filters, execution history and logs.

The plugin uses the device timezone for manual times and display. It does not provide recording buttons, templates, streaming controls or a timezone selector. OBS continues to control the recording format, encoder, destination and sources.

## Windows installation

Builds target **OBS 32.2.2 x64 with Qt 6.11.1**. Other OBS/Qt combinations are not validated.

1. Download the [Windows installer](https://github.com/alexdbdb/obs-scheduler/releases/download/v0.1.0/Broadcast-Scheduler-0.1.0-Setup.exe) from [Releases](https://github.com/alexdbdb/obs-scheduler/releases/tag/v0.1.0).
2. Close OBS and run `Broadcast-Scheduler-0.1.0-Setup.exe`.
3. Select the OBS installation directory and restart OBS.
4. Open **Docks → Broadcast Scheduler**.

For developers, a portable ZIP is also available in successful [Actions builds](https://github.com/alexdbdb/obs-scheduler/actions/workflows/build.yml). It can be extracted into the OBS installation directory, preserving its paths. Uninstalling the plugin retains its database and settings.

Public CI builds do **not** include a Google OAuth client. Manual schedules, recurrences, ICS and the API work without Google. Distributors can follow the [Google registration guide in Spanish](docs/registro-google.md) to build a Google-enabled installer. Ordinary users do not enter client IDs or secrets.

## First recording

Configure a writable recording destination in OBS. Click **Schedule Recording**, choose a start two minutes ahead and an end two minutes later, and save. Ensure the scheduler is enabled. Check the recording and the two transitions in History.

A recording started manually in OBS is left untouched. Overlapping or adjacent scheduled events share a recording; they do not necessarily produce separate files. A start more than 60 seconds late is skipped, and a finished event is never started late. Pausing prevents new starts but preserves stops already scheduled for recordings in progress.

Upgrading from the earlier template-based version pauses existing schedules for review. Each enabled event now means recording from its start to its end.

## Calendars and API

Add an HTTPS ICS URL or a local ICS file in **Calendars**. External events are read-only in the agenda; edit their source. Recurrences are managed separately.

The API is disabled by default and binds to `127.0.0.1`. Generate a token in Settings → API before enabling it. See [API examples](docs/api.md) and [OpenAPI](docs/openapi.json). Keep the API local or on a trusted network; do not expose it directly to the internet.

## Development and limitations

See [build instructions](docs/build.md), [architecture](docs/architecture.md), [Google integration](docs/google-calendar.md) and [validation](docs/validation.md). The project uses C++17, Qt, SQLite and libical. Windows and Linux builds are checked in GitHub Actions; macOS packaging is not validated.

OBS must remain running and the computer awake. There is no automatic wake-up. Encoder failures, storage exhaustion, sleep/resume, ambiguous clock changes and long unattended sessions need further testing. History and logs have no automatic retention policy.

Report reproducible problems in [Issues](https://github.com/alexdbdb/obs-scheduler/issues), including OBS/OS versions and sanitized logs. Do not attach tokens, private calendar URLs or personal recordings. See [contribution guidance](CONTRIBUTING.md).

## License

Maintained by [alexdbdb](https://github.com/alexdbdb) and contributors. Original source is **GPL-2.0-or-later**; the combined open-source binary is distributed under **GPLv3** because it links Qt HTTP Server. The installer displays GPLv3 and bundles the applicable notices and license texts. See [LICENSE](LICENSE), [GPLv3](licenses/GPL-3.0.txt) and [dependency inventory](THIRD_PARTY.md).
