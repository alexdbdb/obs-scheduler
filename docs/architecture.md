# Architecture and behavior

The OBS Qt dock and the OBS adapter run on the UI thread. Runtime, SQLite, the scheduler and asynchronous HTTP/OAuth work run on one worker thread. Calendar parsing uses a dedicated pool. Providers normalize events and never call OBS directly.

## Event model

Events have stable IDs, a source, title, start, end and enabled state. Sources are Manual, API, Recurring, ICS, GoogleCalendar and OdooEvent. Internally times are UTC milliseconds. Manual UI times and API timestamps without an offset use the operating system timezone; explicit offsets preserve their instant. Calendar timestamps retain their source interpretation and are displayed in device time.

Each enabled event produces exactly two actions: `record.start` at its start and `record.stop` at its end. Old template/deferred database structures remain for compatibility, but are not exposed or used to plan recording actions.

## Scheduling and ownership

A precise 250 ms timer checks due work. Execution keys are `event.id/record.start` and `event.id/record.stop`. Editing a previously executed event does not replay an executed action. Starts at the same instant are ordered before stops so adjacent schedules can share one recording.

SQLite claims each execution before dispatch. Claims and requests left after a restart become indeterminate, rather than being replayed. This guarantees at-most-once dispatch, not exactly-once effects in OBS.

A start more than 60 seconds late, or after its event has ended, is skipped. Pausing suppresses new starts. A dispatched start stores its original stop separately, so editing, disabling or removing the source does not discard that stop.

The adapter only stops recordings it intentionally started. A manually started recording is not adopted. Immediately before a scheduled start it temporarily supplies OBS with a sanitized `YYYY-MM-DD - Event name` filename format, then restores the user's profile format; OBS still chooses the directory, extension and collision suffix. Overlapping scheduled events share ownership and the first event names the common file; the last owner stops the recording. Ownership is held in memory and is not restored across OBS restarts. OBS callbacks confirm start and stop. Delayed confirmation remains tracked; a stop already due is applied when a pending start is confirmed.

## Persistence and providers

SQLite uses WAL, FULL synchronous mode, prepared statements and transactional replacement of calendar snapshots. A failed fetch or parse preserves the previous snapshot. Identity for external events is based on calendar and provider instance ID. Old databases are migrated to recording-only schedules and paused for review.

Google Calendar and Odoo Events publish a rolling window from the current instant through seven days ahead, including events already in progress. They refresh once when OBS opens and then at the configured calendar interval. ICS files/HTTPS feeds retain their broader parsing window. Disabled calendars retain cached events but cannot start recordings. Changes to calendar configuration invalidate in-flight snapshots. Recurrence expansion is refreshed hourly.

Google uses user-supplied Desktop OAuth credentials, the system browser, PKCE S256, a random state and a temporary loopback listener. No Google client identity is compiled into the plugin. The client secret and refresh token use OS-protected storage; access tokens remain in memory. Account selection is automatic after authorization. Disconnect disables Google calendars and requests token revocation. See [Google setup](google-calendar.md).

Odoo reads `event.event` directly over HTTPS using a user API key. Odoo 16–18 use JSON-RPC authentication and Odoo 19+ uses JSON-2 bearer authentication. The API key uses OS-protected storage and is never placed in SQLite or UI snapshots. The provider also reads `event.type` and `event.stage` to build local filters; it does not write to Odoo. See [Odoo setup](odoo.md).

## API and UI

The optional API provides status, event CRUD and sync. It is disabled by default, authenticates Bearer tokens against stored SHA-256 hashes and rejects browser Origin requests. Qt handles HTTP parsing; the 1 MiB application body limit applies after receipt. Only loopback binds are allowed. Startup migrates previous network bindings to localhost.

The UI has scheduling, calendars, recurrences, settings, history and logs. Date views filter event lists; they are not drag-and-drop time grids. Recurrences offer daily, weekly, weekday and monthly rules. The dashboard distinguishes a stopped engine from an enabled preference.

There is no machine wake-up or automatic log retention. OBS/encoder failures, disk exhaustion, clock transitions, suspend/resume and long unattended operation still require qualification. See [validation](validation.md).
