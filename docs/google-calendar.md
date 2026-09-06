# Google Calendar setup

The provider uses the official Calendar v3 API with `calendar.readonly` scope. It lists calendars and reads expanded instances (`singleEvents=true`), following pagination. Refresh fetches a complete bounded window; deleted events disappear on successful replacement. It does not write to Google and does not assume Google is the only provider.

1. Create/select a project in Google Cloud Console. Enable **Google Calendar API**.
2. Configure the OAuth consent screen. Add test users if the app is in Testing. For public distribution, complete Google's applicable verification process.
3. Create an OAuth client of type **Desktop app**. Obtain its client ID and client secret. Do not create a Web client or commit the downloaded JSON.
4. In Broadcast Scheduler → Settings → Google, enter those values and click **Connect Google Account**.
5. Approve access in the system browser. The plugin receives the one-time code on `http://127.0.0.1:<ephemeral-port>/oauth/callback`. Keep OBS running during this step. The listener closes on success or after five minutes.
6. Click **Select Google calendars**, enable desired calendars and choose a template for each. Calendars dialog lets you change their refresh interval, timezone and start/end offsets.

PKCE S256 and random OAuth state bind the response to this instance. Access tokens stay in memory; refresh token and desktop client secret are stored using user-bound DPAPI on Windows, Keychain on macOS, Secret Service on Linux. Linux requires an unlocked desktop keyring; absent secure storage causes an explicit failure, never a plaintext fallback. API tokens are unrelated to Google and are stored only as hashes.

Disconnect removes the local refresh token, requests server-side revocation and disables Google calendars. Network revocation can fail if offline; revoke access manually in Google Account → Security if needed. Cached events remain readable but disabled. Blank client-secret field preserves the saved credential.

Troubleshooting: `invalid_grant`/refresh failures require reconnecting; Testing-mode refresh tokens can expire under Google's policies. `redirect_uri_mismatch` usually means the wrong client type. A blocked loopback listener or firewall can prevent callback. Calendar HTTP errors appear without response bodies or tokens in logs. Signed feed URLs and private calendar configuration remain private application data even though they are not OAuth tokens: protect database backups.

Missing for real-account acceptance in this development environment: **developer-owned Desktop client ID/client secret, configured consent screen/test user, and browser consent from that Google account**. No account synchronization is claimed as verified without them.

References: [Desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app), [Calendar list](https://developers.google.com/workspace/calendar/api/v3/reference/calendarList/list), [Events list](https://developers.google.com/workspace/calendar/api/v3/reference/events/list), [Scopes](https://developers.google.com/workspace/calendar/api/auth).
