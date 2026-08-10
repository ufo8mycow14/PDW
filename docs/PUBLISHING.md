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
thread. Event JSON is retained under `PublishQueue` while work is pending.
Transient failures retry up to five times with exponential backoff. Repeated
failures move to `PublishQueue\DeadLetter` for operator review. Pause retains
queued work. The minimum interval setting provides simple rate limiting.

The generated `Published` and `PublishQueue` folders are ignored by Git to
reduce the chance of accidentally committing private traffic. Operators must
also secure backups, web hosting, logs, and any downstream service.
