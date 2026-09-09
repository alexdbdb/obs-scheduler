# Google Calendar

For the complete Spanish setup guide, see [Registrar Google para Broadcast Scheduler](registro-google.md).

End users open Settings → Google → Connect Google Account, authorize read access in their system browser and select calendars. OAuth client ID and secret fields are no longer part of the user interface. Account switching disconnects the previous account and disables its calendars.

The distributor registers one Google Desktop OAuth client and builds with `-GoogleClientFile` (PowerShell) or `GOOGLE_OAUTH_CLIENT_FILE` (CMake), pointing to the downloaded client JSON. Unconfigured builds display an explanatory message. Previously configured clients remain usable; switching accounts uses the bundled client when available.

The plugin requests `calendar.calendarlist.readonly` and `calendar.events.readonly`. It uses PKCE S256, a random state and a temporary loopback listener. The browser opens the Google account chooser; a successful exchange automatically opens calendar selection. Cancellation, denial and timeout allow retry; stale token responses cannot reconnect a disconnected account.

Access tokens remain in memory. Refresh tokens use Windows DPAPI, macOS Keychain or Linux Secret Service. Disconnect removes the local token, requests revocation and disables Google calendars; cached data is retained. Revocation can fail offline.

Google authorization and synchronization with a real account require a registered client and user consent. Local tests do not establish that Google has approved or verified the application.

See the [Google desktop OAuth documentation](https://developers.google.com/identity/protocols/oauth2/native-app) and [Calendar permissions](https://developers.google.com/workspace/calendar/api/auth).
