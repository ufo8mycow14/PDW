# Direct RTL-SDR decoder soak and runtime health gate

Status: review procedure for the direct-source starvation correction. It is not
an installation approval and it does not replace an independent code review.

## What this gate proves

The observed failure is treated as a timing and resource-starvation defect. It
is not evidence of a memory leak. A running process, fresh IQ callbacks, a
moving signal meter, or an apparently strong signal proves only that part of
the receive path is alive. Production health requires fresh **accepted decode
rows** that correlate with an independent, trusted feed during known activity.

Only use a lawful, operator-authorised reference feed. Keep pager content,
addresses, recordings, credentials, and machine-specific paths out of the
repository and review handoff.

## Fixed test configuration

Unless the independent reviewer approves a documented variation, use the
configuration from the reported failure:

- direct RTL-SDR USB receiver;
- 148.8125 MHz;
- 1.024 Msps IQ rate;
- 48 kHz decoder-audio rate;
- 12 kHz NFM bandwidth;
- manual gain 19.7 dB;
- frequency correction 1 ppm; and
- Enhanced IQ filtering and resampling enabled.

Record the receiver identity and executable hash locally. Do not change
filters, protocol interpretation, outputs, or network settings for the test.

## Telemetry to record

The **Signal & radio** diagnostics text exposes the following cumulative and
instantaneous values:

- decoder queue depth, capacity, high-water mark, fixed preallocated bytes,
  dropped blocks/samples, and reset-discarded blocks/samples;
- current and maximum decoder lag, decoded-audio sample count, worker state,
  priority result, quarantine state, stop timeouts, thread starts/stops, and
  worker handle creation/closure/ownership;
- accepted-row count and age of the latest accepted row;
- process handle, GDI-object, and USER-object counts; and
- meter paints, unchanged/skipped updates, suspended updates, tooltip changes,
  and backbuffer object creation/deletion/ownership.

Capture a local screenshot or transcribe counters at the start, after warm-up,
at each remote-session transition, hourly, and after final stop/restart. Redact
all decoded content.

## Deterministic gate before hardware

Both Win32 and x64 builds must pass the complete CTest suite. In particular,
`bounded-audio-decode-worker` must prove all of the following without any UI
queue service:

- a sustained simulated 1.024 Msps IQ callback cadence producing 48 kHz audio
  preserves every sample in order and introduces no reset after startup;
- the fixed queue never exceeds capacity and reports deliberate backpressure;
- a drop makes the next accepted block explicitly discontinuous;
- repeated worker shutdown/restart balances threads and handles; and
- the decoder service clock advances independently of the Windows UI timer.

The maintained POCSAG and FLEX regression tests must also pass unchanged in
both architectures.

## Physical soak procedure

1. Start from an isolated copy approved by the independent reviewer. Warm up
   for 15 minutes with the settings dialog closed, then record all telemetry.
2. Run at least eight hours; 24 hours is preferred. Include repeated Chrome
   Remote Desktop connect, active viewing, disconnect, main-window minimise,
   restore, and settings-open/settings-close cycles. Use at least ten remote
   connect/disconnect cycles spread through the run.
3. During an active trusted-feed window, compare each eligible reference event
   with the visible accepted PDW row. An event is eligible only when it is in
   the receiver's monitored coverage and the unchanged PDW filters are
   configured to display it. Match locally by time, protocol/type, address, and
   redacted content characteristics. A suggested correlation window is plus or
   minus 60 seconds.
4. Continue the active comparison until at least ten eligible trusted events
   have been seen. If traffic is sparse, extend the window; do not reinterpret
   silence as decoder success or failure.
5. Stop and restart the direct source at least 25 times. Finish with the source
   stopped and all optional settings windows closed, then record counters again.

## Pass conditions

All conditions are required:

- every eligible trusted-feed event has a corresponding fresh accepted PDW
  row, with no unexplained gap or stale accepted-row age during known activity;
- the accepted-row counter advances for those matches;
- decoded-audio samples continue advancing, but this is supporting evidence
  only;
- steady-state queue drops and reset discards remain zero; queue depth returns
  toward zero after load bursts and does not remain above half capacity for ten
  seconds;
- decoder lag does not show a rising trend. A 250 ms maximum is the review
  target for this fixed configuration; any one-second excursion or sustained
  500 ms lag requires investigation even if no drop is reported;
- the worker never enters quarantine and reports no stop timeout;
- after each confirmed stop, worker-owned handles return to zero and cumulative
  worker handles created equals handles closed;
- process handles, GDI objects, USER objects, and active meter backbuffer
  objects plateau after warm-up. They must not rise monotonically between
  hourly samples and should return within a predeclared small tolerance of the
  warm baseline after windows close and the source stops; and
- PDW closes cleanly, restarts cleanly, and decodes the next trusted event.

Any unmatched trusted event, queue drop, unaccounted decoder reset, quarantine,
stop timeout, monotonic resource growth, or inability to reproduce a clean
restart fails the gate. IQ freshness or process activity cannot override that
failure.

## Evidence bundle for independent review

Retain locally:

- exact commit/review-diff identity and executable SHA-256;
- Win32/x64 build and test logs;
- start/hourly/end telemetry with remote-session transition times;
- a redacted correlation table containing trusted-event time, PDW-row time,
  match result, and reviewer note; and
- any failed interval with the immediately preceding queue, lag, accepted-row,
  and resource counters.

Do not install on the Lounge PC until the independent reviewer signs off the
code diff and this physical runtime gate passes.
