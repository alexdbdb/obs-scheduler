# Google Calendar

Read the complete [English setup guide](google-setup.md) or [Spanish guide](registro-google.md).

Each user creates a Desktop OAuth client in their own Google Cloud project, enters its Client ID and client secret in Settings → Google, connects their account in the system browser and selects calendars. The client secret and refresh token are kept in OS-protected credential storage, never in the database. Credential changes disconnect the previous account and disable its calendars.

No Google identity is compiled into the plugin. Source builds, CI artifacts and installers are therefore independent from the repository maintainer's Google account and Cloud project.

The plugin requests `calendar.calendarlist.readonly` and `calendar.events.readonly`. It uses PKCE S256, a random state and a temporary loopback listener. The browser opens the Google account chooser; a successful exchange automatically opens calendar selection. Cancellation, denial and timeout allow retry; stale token responses cannot reconnect a disconnected account.

Selected calendars synchronize when OBS opens and then at their configured refresh interval. Each successful refresh replaces the local snapshot with events overlapping the rolling period from now through seven days ahead, including events already in progress. This keeps the local schedule bounded while the horizon advances automatically.

Access tokens remain in memory. Refresh tokens use Windows DPAPI, macOS Keychain or Linux Secret Service. Disconnect removes the local token, requests revocation and disables Google calendars; cached data is retained. Revocation can fail offline.

Google authorization and synchronization with a real account require the user's registered client and consent. Local tests do not establish that Google has approved or verified any user's project.

See the [Google desktop OAuth documentation](https://developers.google.com/identity/protocols/oauth2/native-app) and [Calendar permissions](https://developers.google.com/workspace/calendar/api/auth).
