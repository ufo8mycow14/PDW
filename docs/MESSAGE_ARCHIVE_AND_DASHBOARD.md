# Capcode directory, message history, and local dashboard

PDW now has one local SQLite archive for operator-maintained capcode names and,
when explicitly enabled, decoded-message history. Open **Filters > Capcode
directory** to add, search, import, or export mappings. A seven-digit number
beginning with `1` is common in paging systems, but PDW deliberately accepts
other numeric lengths used by its supported protocols. The raw decoded capcode
is never replaced by its display name.

Each mapping can apply to one protocol or all protocols and can include a
display name, agency or service, colour, enabled state, and notes. Protocol-
specific mappings win over an all-protocol mapping. CSV import and export use
these columns:

```text
protocol,address,display_name,agency,color,notes,enabled
```

Open **View > Message history** to enable the local archive, choose whether
message text may be retained, set a 1-3650 day retention period, and search or
page through stored events. History is disabled by default and message text is
also excluded by default. The database is `pdw-history.sqlite3` beside PDW
unless another path is selected. Purging history does not delete the capcode
directory. Every opened archive requires current Windows SQLite security
controls, disables trusted schema execution, extension loading, triggers,
views, and memory-mapped I/O, enables defensive and cell-size checks, and must
pass a bounded quick integrity check as its first SQL before PDW creates tables
or writes data. Invalid or corrupt database files are refused without being
repaired or replaced. PDW also stamps its archive application ID and schema
version; a valid SQLite file containing unrelated tables is rejected unchanged
instead of being converted into a PDW archive.

Open **Outputs > Local live dashboard** to enable a read-only browser view.
The dashboard is disabled by default, requires message history, binds only to
`127.0.0.1`, and rejects Host headers other than `localhost` or `127.0.0.1` on
its configured port. It cannot accept remote connections or change PDW. The
GET-only endpoints are `/api/v1/messages`, `/api/v1/capcodes`, and `/health`.
The messages endpoint returns the fields actually retained by local history;
live-only routing metadata that is not stored is omitted rather than reported
with invented default values. Archive writes use a bounded best-effort queue.
On shutdown, PDW completes at most the write already in progress and discards
the queued tail so a locked archive cannot hold the application open.

Capcodes, names, agencies, notes, and message text may identify people or
services. Only collect and retain data that you are authorised to monitor. The
existing publishing and data-output address masking also removes names and
agencies so an alias cannot defeat the privacy setting.
