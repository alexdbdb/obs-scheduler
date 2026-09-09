# Contributing

This project is a prerelease recording scheduler maintained by alexdbdb and contributors.

Open an issue with a reproducible example before proposing a large feature. Include OBS, Qt and operating system versions. Remove account tokens, private calendar links, personal event data and recordings from reports.

Build using [the documented workflow](docs/build.md) and run the scheduler/API tests. For OBS output changes, also describe the manual or isolated smoke test performed. Do not present mocked or local OAuth tests as real Google account validation.

Contributions should keep recording scheduling simple. Providers publish normalized events; the OBS adapter owns output calls. Use device-local time for user input and UTC instants internally. Preserve existing user databases and ownership protection.

Source contributions are provided under GPL-2.0-or-later, consistent with this repository. Generated files, build directories, downloaded dependencies and credentials must not be committed.
