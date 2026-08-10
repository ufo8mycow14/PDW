# Publishing and web integration

Publishing is an optional output inside PDW. It is disabled by default. Before
it can be enabled, the operator must acknowledge responsibility for checking
permissions, privacy obligations, radio laws, and publication laws in their
own country or jurisdiction. PDW cannot determine whether intercepted traffic
may legally be received or republished in every location.

## Static files

PDW maintains five UTF-8 files in the selected folder:

- `messages.json`: the most recent events as a JSON feed;
- `messages.jsonl`: one JSON event per line for scripts and imports;
- `messages.rss`: an RSS 2.0 feed;
- `messages.atom`: an Atom feed;
- `index.html`: a responsive standalone table for a simple website.

The rolling JSON, RSS, Atom, and HTML feeds restore up to the newest 200 unique
events from JSONL after restart. A torn final JSONL line is removed before a
durable retry, and every static file is flushed before that destination is
recorded complete.

Point **File transfer** at these files to publish them using the existing FTP,
certificate-verified FTPS, or host-key-verified SFTP scheduler. A web server can
also serve the folder directly.

## HTTPS webhook

The webhook sends one JSON object per event and refuses non-HTTPS URLs. Normal
Windows/libcurl certificate validation stays enabled. A bearer token adds an
`Authorization: Bearer` header. An HMAC secret adds
`X-PDW-Signature: sha256=<hex digest>` over the exact request body. Both
secrets are stored in Windows Credential Manager, not `pdw.ini`. Each request
also carries an `Idempotency-Key` matching the event ID.

**Test webhook** sends a configuration-only test object and no decoded pager
text. New secrets used by a test remain in memory only and are wiped after the
test; **Save** is the only action that writes them to Credential Manager. Blank
secret fields on Save leave existing saved secrets unchanged.

## Privacy transforms

PDW first copies the decoded event. Source aliasing, pager-address masking, and
message-text omission apply to that published copy only. The source message in
PDW's display, filters, legacy logs, email, and Apprise path is not mutated.
Choose **Filtered messages only** unless publication of every displayed decode
is explicitly intended and lawful.

## Queue and failure behaviour

Delivery runs on a background worker and never waits in the capture/decoder
thread. A version-2 `.pdwjob` record is atomically retained under
`PublishQueue` before normal work is enqueued. It preserves the original event
ID, selected destinations, completed and exhausted destinations, independent
per-destination attempts, and the static output folder selected at intake. A
restart therefore does not change the idempotency key, repeat a completed
destination, reset a five-attempt limit, or silently move older queued traffic
to a newly selected website folder. Version-1 jobs and older payload-only
`.json` queue files remain loadable and are conservatively upgraded without
taking their identity from the filename.

Each destination retries up to five times with exponential backoff. Exhausting
one destination does not prevent another selected destination from completing;
the final job moves to `PublishQueue\DeadLetter` when all selected destinations
are terminal and at least one failed. Pause retains existing and newly arriving
queued work. Temporarily unavailable HTTPS support holds webhook work without
consuming retries, while static work remains available. Duplicate pending event
IDs are suppressed; malformed, oversized, duplicate, and excess recovery files
are quarantined rather than sent. A flushed temporary record can replace an
older final record only when its completion/failure state advances
monotonically. The minimum interval setting provides simple rate limiting.
Disposable-service acceptance is still required to classify terminal
certificate/authentication failures separately from transient timeouts and
service throttling.

The generated `Published` and `PublishQueue` folders are ignored by Git to
reduce the chance of accidentally committing private traffic. Operators must
also secure queue and dead-letter files because they can contain the selected
published message representation and its selected static output path, as well
as backups, web hosting, logs, and any downstream service.
