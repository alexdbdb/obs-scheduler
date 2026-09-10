# Preparing and publishing a release

Release target: Windows x64, OBS 32.2.2 / Qt 6.11.1. This is the initial community release. Do not claim all OBS 32 versions, Linux runtime or macOS support.

## Before the first public release

1. Retire the Google Desktop OAuth client used by the 0.1.0 installer in its owning Google Cloud project. This affects accounts using that old client; reconnect them with their own clients.
2. Remove or replace the obsolete 0.1.0 installer assets on GitHub. Audit downloaded historical binaries separately before making them available again.
3. Run the credential audit with --history. Investigate any finding privately. Rewrite history only if an actual secret is found; the old credential-template code alone does not require history rewriting.
4. Enable private vulnerability reporting in GitHub repository settings.
5. Confirm that all intended changes are committed and the worktree is clean. Use a new version/tag; never retag 0.1.0.

These account-side actions require the maintainer's Google Cloud and GitHub access. A successful local build does not perform them.

## Build and verify

1. Set the same release version in CMake, the Windows build script, installer and OpenAPI metadata. Run the locale check.
2. Build using the Windows script or installed-OBS helper described in [build.md](build.md).
3. Review core/API tests, locale checks, the package credential/path audit and the isolated OBS loader/TLS test when an installed OBS is available.
4. Install and uninstall in a disposable Windows account or VM with a clean OBS profile. Verify English and Spanish, high DPI, Google/Odoo connections, recording filenames, startup refresh and removal of an imported event. Never claim simulated provider tests as real account validation.
5. Review [validation.md](validation.md), compatibility and limitations. Include a real screenshot of the scheduler with synthetic events, not private calendar data.
6. Publish checksums and corresponding source with the installer and ZIP. The source must match the exact tagged commit and include the build scripts, license notices and dependency sources described in [source-distribution.md](source-distribution.md).

## GitHub release

Push a reviewed v0.2.0 tag after the account-side checks and qualification. The build workflow runs tests and creates a **draft release** with Windows packages, corresponding sources and checksums. Review all assets and release notes before publishing the draft. The workflow never changes repository visibility.

Only make the repository public after the historical-client and release-asset checks are complete. The installer is currently unsigned; users may see Windows reputation warnings. Signing can be added when a publisher certificate is available.

## OBS community listing

Use the [OBS community resources](https://obsproject.com/forum/resources/) and clearly describe this as an independent community plugin. Suggested English description:

> Schedule OBS recordings from manual events, recurring schedules, ICS, Google Calendar or Odoo Events. Google and Odoo synchronize directly using your own credentials, with a rolling seven-day window. Includes English and Spanish interfaces. Windows x64 release, tested against OBS 32.2.2. OBS must remain running and the computer awake.

Include screenshots, installation/upgrade instructions, the exact supported OBS version, source/license links, known limitations and a support link. Do not imply OBS Project endorsement.
