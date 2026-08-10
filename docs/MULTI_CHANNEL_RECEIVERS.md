# Guarded multi-channel receivers

Open **Outputs > Guarded multi-channel receivers** to configure up to four
additional simultaneous receiver channels. This mode is off until the operator
enables a slot and chooses **Start enabled**.

PDW's established decoders contain process-global timing and slicer state.
Consequently, each additional channel is run in an isolated PDW worker process
instead of sharing decoder globals inside the main process. This preserves the
normal single-source decoder path and lets Windows contain a receiver or
decoder failure to its channel.

Each channel requires either:

- its own `rtl_tcp` host and port; or
- its own direct RTL-SDR device index.

Two workers cannot reuse the same endpoint or device, and a worker cannot reuse
the receiver currently selected in the main window. Host comparison is
case-insensitive and treats `localhost`, `127.0.0.1`, and IPv6 loopback as the
same local endpoint without performing a DNS lookup. This release does not
channelise several paging frequencies from one wideband IQ stream; one
wideband receiver must expose separate tuned endpoints, or separate receivers
must be used.

Local message history must be enabled before workers can start. Worker events
include a channel source label and are written to the shared local SQLite
history. For safety, worker processes disable publishing, data outputs, SMTP,
FTP, Apprise, Windows notifications, Telnet, the dashboard listener, and legacy
log files. They cannot save over the main `PDW.INI`. Close the main PDW window
or choose **Stop all** to request an orderly close of its workers. Each worker
is assigned to a per-slot Windows job before it starts; if orderly close or a
partial-launch rollback misses a not-yet-created window, the job's
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` boundary prevents the worker or its child
processes from being left running after its manager exits. Shutdown allows
three seconds for orderly close, then uses a bounded two-second forced-stop
fallback before releasing all tracked process and job handles.
PDW rejects saving a changed or disabled slot while its worker is running;
choose **Stop all**, edit and save the slots, then choose **Start enabled** so
the displayed configuration cannot diverge from the active receiver.
