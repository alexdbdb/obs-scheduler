# Connect your own Google Calendar

[Español](registro-google.md)

Each user creates a Google Cloud project and supplies a **Desktop app Client ID and client secret**. Broadcast Scheduler has no shared Google project, developer account or intermediary service. The client secret and account tokens use protected operating-system storage; they are not saved in the scheduler database.

## Create the project and enable Calendar

Open [Google Cloud Console](https://console.cloud.google.com/projectcreate), create a project and keep it selected throughout setup. Enable the [Google Calendar API](https://console.cloud.google.com/apis/library/calendar-json.googleapis.com). You do not need Gmail, Drive or a service account.

## Configure Google Auth Platform

Open [Google Auth Platform](https://console.cloud.google.com/auth/overview). Complete its setup with an application name, support email and developer contact email.

In [Audience](https://console.cloud.google.com/auth/audience), choose External for a personal Gmail account. Internal is for accounts within your own Google Workspace organization. While the project is in **Testing**, add the exact account you will connect under **Test users**.

In Data Access, add both read-only scopes:

~~~text
https://www.googleapis.com/auth/calendar.calendarlist.readonly
https://www.googleapis.com/auth/calendar.events.readonly
~~~

The plugin uses these to list calendars and read events. It cannot edit or delete events in Google. Selecting calendars in Scheduler controls which events are imported; it does not narrow the account permission granted by Google.

## Create a Desktop app client

Under Clients, create an OAuth client with application type **Desktop app**. Copy its Client ID (ending in .apps.googleusercontent.com) and client secret. Do not choose Web application or UWP.

In OBS, open **Broadcast Scheduler → Settings → Google**, paste both values and click **Connect Google Account**. Grant both calendar permissions in your browser and return to OBS to select calendars.

No JavaScript origins or fixed callback port are needed. The plugin uses a temporary local callback with PKCE, following Google's [desktop authorization flow](https://developers.google.com/identity/protocols/oauth2/native-app). Google also provides a [credential creation guide](https://developers.google.com/workspace/guides/create-credentials).

The secret field is blank when settings reopen. Leave it blank to retain the saved secret. Changing the client disconnects the old account and disables its calendars. Disconnect removes local account tokens and requests their revocation.

## Keep the connection working

With calendar scopes, authorizations for an External project in **Testing** expire after seven days. For ongoing personal scheduling, review the project's Audience and production status. Moving to production is not the same as obtaining Google verification: warnings, Workspace restrictions and verification requirements can still apply. See Google's [audience guidance](https://support.google.com/cloud/answer/15549945?hl=en) and [sensitive-scope guidance](https://developers.google.com/identity/protocols/oauth2/production-readiness/sensitive-scope-verification).

Keep your client credentials private and use your own project. Sharing a Client ID with other people makes you responsible for that Google application.

## Troubleshooting

- **403 access_denied / app is being tested:** select the project owning your Client ID, then add the exact account under Audience → Test users. Workspace administrators may also restrict access.
- **403 when loading calendars:** enable Google Calendar API in that same project, wait a few minutes, and synchronize again.
- **client_secret is missing / HTTP 400:** enter the secret from the same Desktop app client as the Client ID.
- **redirect_uri_mismatch:** check that the client type is Desktop app.
- **Missing permissions:** reconnect and grant both calendar permissions.
- **Connection stops after a week:** check whether your project is still in Testing.
- **No upcoming events:** check calendar selection, event dates and timezones. Google imports a rolling seven-day window, including events already in progress. OBS must remain open for periodic updates.

Use a dedicated test calendar and a short future event to verify recording start, stop and History before relying on unattended operation.
