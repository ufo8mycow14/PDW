# Optional data outputs

**Settings > Connections and automation > Data outputs** adds modern consumers
beside PDW's existing email, Apprise, FTP, static-feed, and webhook paths. It
does not replace any of them. Every adapter is disabled by default, shares the
same immutable decoded-event copy, and runs away from the capture/decoder
thread on a bounded worker queue.

Enabling the group requires the operator to acknowledge responsibility for
permissions, privacy obligations, radio laws, and data-sharing laws in their
country or jurisdiction. **Filtered messages only**, address masking, and
message-text omission are the privacy-safe defaults. Those transforms affect
only the optional output copy; they do not alter PDW's display, filters, logs,
email, or Apprise behaviour.

## MQTT

PDW uses its already-pinned libcurl build for MQTT 3.1.1 publishing. The broker
URL and topic are configured separately, credentials cannot be embedded in the
URL, and the password is held in Windows Credential Manager. `mqtts://` uses
normal certificate and hostname verification. Plain `mqtt://` requires an
explicit trusted-local-network opt-in.

This libcurl transport publishes at QoS 0 and does not set MQTT retain. The UI
states both limits so PDW does not imply delivery guarantees that the selected
transport cannot provide.

## SQLite

SQLite uses the Windows system SQLite component and creates a UTF-8 database
with a conservative table name, WAL journalling, useful indexes, prepared
statements, and event IDs as primary keys. Replaying the same event ID is
idempotent. A relative database path stays beside PDW; no database is created
until SQLite is enabled or its test row is requested.

## MySQL through ODBC

MySQL support uses a Windows ODBC DSN rather than bundling an old MySQL wire
protocol or authentication implementation. Install a current MySQL ODBC driver
and configure server, database, certificate/TLS policy, and other transport
security in the DSN. PDW supplies the DSN, username, and Credential Manager
password, sets `utf8mb4`, creates the selected table when permitted, and uses
prepared inserts. A missing driver or DSN affects only this adapter.

## Telnet JSON

The Telnet adapter is a read-only stream of one UTF-8 JSON object per line. It
does not accept commands or feed bytes into PDW. The default bind address is
`127.0.0.1`; a non-loopback bind is refused unless **Allow non-loopback clients
(unencrypted)** is explicitly selected. Telnet is plain text, so remote use
should be limited to a trusted, separately protected network.

## Windows notifications

Native Windows notification support is loaded lazily only when the adapter is
enabled or tested. The required per-user application identity is registered at
that point without administrator access. Message text has a separate opt-in
because notifications may be visible on the lock screen. Turning the adapter
off leaves PDW's legacy tray behaviour unchanged.

## Testing and failure isolation

Each tab has a configuration test. Test events contain synthetic values and no
live pager message. SQLite and MySQL tests deliberately write one identified
test row; MQTT sends one synthetic JSON object; Telnet tests bind/listen; and
Windows shows a synthetic notification. The queue is bounded at 500 events.
An adapter error is recorded for status reporting and never blocks decoding or
prevents another selected adapter from receiving the same event.
