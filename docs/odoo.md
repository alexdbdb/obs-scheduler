# Odoo Events

Broadcast Scheduler can read Odoo Events directly, without a bridge service. It imports `event.event` records into the same read-only external-event model used by calendar providers. Deleting an imported item in Scheduler hides it locally; it does not delete the record in Odoo.

## Compatibility

- Odoo 16–18: select **JSON-RPC** and provide the server URL, database name, user login and that user's API key.
- Odoo 19 and later: select **JSON-2** and provide the server URL, database name and API key. The login field is not sent by JSON-2.

Only HTTPS endpoints are accepted. The server must be reachable directly from the computer running OBS. The Odoo user needs read access to `event.event`, `event.type` and `event.stage`; normal Odoo record rules and company restrictions still apply.

## Create and configure the key

Sign in to Odoo as the dedicated user, open the account security/preferences page and create an API key. Odoo only displays the key when it is created, so copy it then. A dedicated least-privilege user is preferable to an administrator account.

In OBS, open **Broadcast Scheduler → Settings → Odoo**:

1. Enter the public base URL, such as `https://odoo.example.com`.
2. Enter the exact database name. This is required even when the URL serves only one database.
3. For Odoo 16–18, enter the user's login and choose **JSON-RPC**. For Odoo 19+, choose **JSON-2**.
4. Paste the API key and choose **Test and select filters**.
5. Select the event types, stages and kanban states to import, then save.

The API key is encrypted using the operating system's protected credential storage. It is not saved in the Scheduler SQLite database, emitted in UI state or written to logs. Leaving the key field blank preserves the stored key. Changing the URL, database, login or protocol requires entering it again and clears the previous Odoo snapshot and local exclusions so records from different Odoo instances cannot be mixed.

## Synchronization behavior

The plugin reads active events whose dates overlap a rolling range from now through seven days ahead. Events that began earlier but are still in progress are included. Synchronization runs when OBS opens and then at the configured interval, so the seven-day horizon advances automatically without accumulating a large calendar. Results are paginated up to 5,000 events. Dates returned by Odoo are interpreted as UTC, matching Odoo's external API representation, and displayed in the OBS computer's local timezone.

The filter dialog reads event types and stages from Odoo. All options are selected the first time. Once filters are saved, clearing every option in any one group intentionally imports no events. The three Odoo kanban values are in progress (`normal`), ready for the next stage (`done`) and blocked (`blocked`).

A failed request preserves the last successful local snapshot and records a sanitized error in Scheduler logs. Disconnecting removes the local API key and managed Odoo calendar. It does not revoke the key on the Odoo server; revoke it from Odoo if it may have been exposed.

Official references: [Odoo 16 external API](https://www.odoo.com/documentation/16.0/developer/reference/external_api.html) and [Odoo 19 JSON-2 API](https://www.odoo.com/documentation/19.0/developer/reference/external_api.html).
