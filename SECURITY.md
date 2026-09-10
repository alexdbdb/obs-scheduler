# Security policy

The 0.2 beta series is the current development line. Version 0.1.0 is obsolete and should not be redistributed. Security fixes will target the latest release; there is no long-term-support branch.

## Report a vulnerability

Use GitHub's **Security → Report a vulnerability** for this repository when private reporting is available. If the button is unavailable, open an issue containing only a request for a private reporting channel. Do not include exploit details, credentials or personal data in that public request.

Include the affected version, platform, reproduction steps and impact through the private channel. Never send real Google tokens, client secrets, Odoo API keys, calendar URLs with access tokens, scheduler databases or OBS profiles.

## Data and network boundaries

- No telemetry or developer-controlled calendar service is used.
- Google and Odoo credentials belong to each user. Tokens and secrets use Windows DPAPI, macOS Keychain or Linux Secret Service. Provider identity settings and imported event contents are stored in the local SQLite database.
- Local event exclusion does not delete events at the source.
- The control API is disabled by default, requires a Bearer token and accepts loopback addresses only. Previous non-loopback configurations migrate to localhost at startup. It is not an internet server.
- Calendar links and remote service responses can contain private data. Sanitize diagnostics before sharing them. OS credential protection does not protect against other processes running as the same user.
- Removing the plugin retains the scheduler database and protected credentials. Disconnect providers and remove the data directory yourself if you want to erase local scheduler data.

## Historical Google client

Version 0.1.0 supported embedding a Desktop OAuth client in distributed binaries. A desktop Client ID/secret is not an account password and does not itself grant calendar access, but distributing it associates authorization with that Google project. The maintainer must retire the old client and old release assets before public launch.

Run the credential audit before releasing:

~~~sh
python scripts/release-audit.py --history
~~~

This redacts findings and scans known credential formats in reachable Git objects and non-ignored working files. It is a targeted check, not proof that arbitrary credentials or downloaded historical release binaries are absent.
