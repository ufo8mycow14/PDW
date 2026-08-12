# Local Gateway Outbox

PDW can optionally write a one-way, local SQLite outbox for a separate gateway
process. It is disabled by default. Enabling it does not enable Publishing,
MQTT, webhooks, Firebase, Google Cloud, or any other network connection.

The outbox receives decoder-finalized events before per-capcode destination
routing is applied. Filter matches, rejected messages, duplicate suppression,
group calls, fragments and reconstructed messages are stored as event metadata.
They do not change PDW's established display, logging, archive, notification or
output behavior.

## Operator setup

Open **Settings > Data outputs > Local Gateway Outbox**. Set a stable,
operator-approved receiver ID, SQLite path, retention period, maximum size and bounded queue capacity, then
enable and save. The writer runs on its own bounded worker. A database error,
full queue or low-disk condition is reported in the dialog and never stops the
decoder.

The **Generate synthetic gateway event** button writes one fixed invented
POCSAG or FLEX event directly to this outbox. It bypasses the decoder, sets
`synthetic=1`, and does not invoke any other PDW output.

## Reader contract

The database uses SQLite WAL mode and application ID `0x50444757` (`PDGW`).
Schema changes are additive and tracked by `PRAGMA user_version`; the current
database schema version is 2 and canonical event schema version is 1.

Gateway processes must:

1. Open `file:<path>?mode=ro` with `SQLITE_OPEN_READONLY | SQLITE_OPEN_URI`.
2. Set `PRAGMA query_only=ON`.
3. Read `gateway_events` in ascending `receiver_sequence` order.
4. Keep their checkpoint in their own storage, never in PDW's outbox.
5. Treat sequence gaps as observable local drops and `event_id` as the stable
   idempotency key.

The `-wal` and `-shm` files must remain beside the database while PDW is open.
Copying only the main database is not a safe live-reader mechanism.

`gateway_events` is append-only except for local age/size retention. An update
trigger prevents mutation of committed canonical events. Cloud acknowledgement
never controls retention.

## Diagnostics

The dialog exposes queue depth and high-water mark, assigned and committed
sequences, the oldest retained sequence, drops/gaps, write failures, retained
record count, database size, free disk space, a low-disk warning and the last
writer error. No decoded message text is copied into diagnostic history.
