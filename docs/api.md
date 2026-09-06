# Local API v1

Settings → API → Regenerate token; copy the token once. Enable API and save. Default URL `http://127.0.0.1:8766`; Bearer authentication is required for every defined route. No browser CORS support. API token grants create/edit/delete and native output control; keep it private. Non-loopback addresses require explicit warning acknowledgement. Use only trusted local networks; HTTPS is not built in.

| Method | Path | Result |
| --- | --- | --- |
| GET | /api/v1/status | Version, scheduler flag, OBS states |
| GET | /api/v1/events | `{ "events": [...] }` |
| GET | /api/v1/events/{id} | Event |
| POST | /api/v1/events | Create API event, HTTP 201 |
| PUT | /api/v1/events/{id} | Replace writable event, HTTP 200 |
| DELETE | /api/v1/events/{id} | Delete writable event |
| GET | /api/v1/templates | `{ "templates": [...] }` |
| POST | /api/v1/actions/record/start | Request recording start |
| POST | /api/v1/actions/record/stop | Request ownership-guarded stop |
| POST | /api/v1/actions/stream/start | Request streaming start |
| POST | /api/v1/actions/stream/stop | Request ownership-guarded stop |
| POST | /api/v1/sync | Start provider refresh, HTTP 202 |

```sh
curl -X POST http://127.0.0.1:8766/api/v1/events \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  --data '{"title":"Artist X","start":"2026-09-10T21:00:00+02:00","end":"2026-09-10T23:00:00+02:00","timezone":"Europe/Madrid","template":"Concert","enabled":true}'
```

On PowerShell use `curl.exe` and preferably `--data-binary @event.json` to avoid shell JSON quoting differences. `template` accepts the template ID or exact name (case-sensitive). The response supplies the ID for subsequent GET/PUT/DELETE. PUT is a full replacement: include title/start/end/template and the other fields you want retained. POST always creates a fresh ID; API clients needing idempotent updates must retain the returned ID and use PUT. External and recurring events return 409 for mutation. Do not send external_id/source/source_calendar to impersonate providers; those are server-controlled. Dates require ISO 8601 Z or explicit offset. Timezone defaults to UTC and controls display/recurrence context, not the meaning of an explicit offset.

Errors use `{ "error": "clear message" }`: 400 validation/invalid JSON, 401 missing/invalid token, 403 browser Origin, 404 missing event, 405 unsupported method, 409 read-only source, 413 application body limit, 500 internal failure. Successful action responses contain `result` and `message`; `requested` does **not** imply that OBS has completed the transition. Follow status/history and OBS error messages. Stops are still subject to output ownership. API start/stop are explicit operator commands, not scheduled events; timed stop policies apply to the scheduler, while these endpoints request immediate control.

The API never edits templates, credentials or server configuration; those remain local UI operations. Token regeneration invalidates the previous token. See [OpenAPI](openapi.json) for a machine-readable description.
