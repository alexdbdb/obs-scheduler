# Architecture and behavior

The OBS Qt dock and the OBS adapter run on the UI thread. Runtime, SQLite, the scheduler and asynchronous HTTP/OAuth work run on one worker thread. Calendar parsing uses a dedicated pool. Providers normalize events and never call OBS directly.

## Event model

Events have stable IDs, a source, title, start, end and enabled state. Sources are Manual, API, Recurring, ICS and GoogleCalendar. Internally times are UTC milliseconds. Manual UI times and API timestamps without an offset use the operating system timezone; explicit offsets preserve their instant. Calendar timestamps retain their source interpretation and are displayed in device time.

Each enabled event produces exactly two actions: `record.start` at its start and `record.stop` at its end. Old template/deferred database structures remain for compatibility, but are not exposed or used to plan recording actions.

## Scheduling and ownership

A precise 250 ms timer checks due work. Execution keys are `event.id/record.start` and `event.id/record.stop`. Editing a previously executed event does not replay an executed action. Starts at the same instant are ordered before stops so adjacent schedules can share one recording.

SQLite claims each execution before dispatch. Claims and requests left after a restart become indeterminate, rather than being replayed. This guarantees at-most-once dispatch, not exactly-once effects in OBS.

A start more than 60 seconds late, or after its event has ended, is skipped. Pausing suppresses new starts. A dispatched start stores its original stop separately, so editing, disabling or removing the source does not discard that stop.

The adapter only stops recordings it intentionally started. A manually started recording is not adopted. Overlapping scheduled events share ownership; the last owner stops the recording. Ownership is held in memory and is not restored across OBS restarts. OBS callbacks confirm start and stop. Delayed confirmation remains tracked; a stop already due is applied when a pending start is confirmed.

## Persistence and providers

SQLite uses WAL, FULL synchronous mode, prepared statements and transactional replacement of calendar snapshots. A failed fetch or parse preserves the previous snapshot. Identity for external events is based on calendar and provider instance ID. Old databases are migrated to recording-only schedules and paused for review.

ICS files/HTTPS feeds and Google Calendar publish a rolling window from seven days ago through 400 days ahead. Disabled calendars retain cached events but cannot start recordings. Changes to calendar configuration invalidate in-flight snapshots. Recurrence expansion is refreshed hourly.

Google uses Desktop OAuth in the system browser, PKCE S256, a random state and a temporary loopback listener. Account selection is automatic after authorization. Access tokens remain in memory; refresh tokens use OS-protected storage. Disconnect disables Google calendars and requests token revocation. Public builds without client configuration show Google as unavailable. See [Google setup](google-calendar.md).

## API and UI

The optional API provides status, event CRUD and sync. It is disabled by default, authenticates Bearer tokens against stored SHA-256 hashes and rejects browser Origin requests. Qt handles HTTP parsing; the 1 MiB application body limit applies after receipt. Use only localhost or a trusted network.

The UI has scheduling, calendars, recurrences, settings, history and logs. Date views filter event lists; they are not drag-and-drop time grids. Recurrences offer daily, weekly, weekday and monthly rules. The dashboard distinguishes a stopped engine from an enabled preference.

There is no machine wake-up or automatic log retention. OBS/encoder failures, disk exhaustion, clock transitions, suspend/resume and long unattended operation still require qualification. See [validation](validation.md).
