# PDW modernization roadmap

PDW has reliable, field-tested decoder logic coupled to a legacy Win32
interface and hardware model. Modernization is therefore incremental: preserve
known decoding behaviour, establish repeatable evidence, and change one
boundary at a time.

## Non-negotiable compatibility boundaries

- Ship one `PDW v4.1.0 Beta.exe`, one settings experience, and one consolidated message
  stream. Do not create separate legacy/enhanced editions or helper services.
- Do not alter protocol algorithms without representative signal recordings
  and before/after decoder-output comparisons.
- Keep the Win32 build available while the serial slicer and its x86
  dependencies remain supported.
- Retain WinMM audio, serial input, two- and four-level slicers, `.rec`
  playback, INI settings, filters, logs, WAV alerts, and all existing protocol
  parsers. Enhanced capture and decoding run alongside them, never instead of
  them.
- Preserve existing `pdw.ini`, `filters.ini`, log, recording, and WAV-file
  behaviour unless a migration is documented and tested.
- When legacy and enhanced decoder candidates agree, show one message. When
  only one succeeds, retain it. When valid candidates conflict, retain the
  ambiguity for operator review rather than silently discarding either result.
- Never use live pager traffic as public test data. Test fixtures must be
  synthetic, redacted, or explicitly licensed for redistribution.

## Phase 1: dependable current build

- Centralize version metadata.
- Keep Visual Studio 2022 CI green and publish a build artifact.
- Document a reproducible local Win32 build.
- Build third-party dependencies from checksum-verified, pinned source
  releases rather than committing opaque binary bundles.
- Remove obsolete Windows XP product metadata.
- Add small operator improvements that do not touch decoder algorithms.

## Phase 2: regression evidence

- Add licensed or synthetic WAV/recording fixtures for every supported mode.
- Capture expected decoded messages, bit-error counts, and filtering results.
- Extract pure protocol components from global UI state where practical.
- Run decoder regression tests in CI before accepting algorithm changes.

Current foundation:

- `audio_signal_core` provides tested PCM normalization, adaptive DC/amplitude
  tracking, and two-/four-level symbol slicing behind a pure internal boundary.
- `decoder_candidate_core` provides tested agreement, single-result retention,
  and conflict preservation using capture-assigned transmission identities.
- Normalized samples are connected to the live input boundary. For four-level
  audio FLEX, the hybrid symbol mapping preserves the original two-level sign
  decision used by phases A/C and adds only the enhanced inner/outer decision
  used by phases B/D. Low-confidence input falls back to the unchanged legacy
  0/3 symbol path.
- The candidate consolidator remains the required boundary for a future second
  full protocol parser (for example soft-decision BCH). It is tested but is not
  used to imply that two independent parsers exist today.
- Custom audio arrays are bounds-safe and malformed legacy INI values are
  clamped to the same values exposed by the existing UI.

## Phase 3: Windows experience

- Add event-driven WASAPI capture beside WinMM, with automatic fallback to the
  unchanged WinMM path and clear device-loss recovery.
- Use a float/16-bit 48 kHz internal representation for enhanced inputs while
  retaining the existing unsigned 8-bit sample path.
- Audit Windows 10/11 audio-device selection and hot-plug behaviour.
- Add DPI-aware layout work only after dialog and main-window visual tests
  exist.
- Replace obsolete WinHelp integration with maintained documentation.
- Improve configuration discoverability while retaining INI compatibility.

Implemented in 4.1.0 Beta: event-driven WASAPI fallback and recovery, a shared
normalized sample sink for modern inputs, actual-device smoke coverage, and a
visually verified Radio and Signal Sources dialog. WinMM remains first choice.

## Phase 4: safety and maintainability

- Replace high-risk unbounded string operations in externally influenced paths.
- Separate capture, decoding, message formatting, and presentation boundaries.
- Add structured diagnostic logging that excludes decoded message content by
  default.
- Evaluate x64 only after legacy serial/hardware dependencies are isolated.

## Phase 5: enhanced decoding beside legacy

- Feed normalized samples into adaptive filtering, clock recovery, and both
  two- and four-level slicers without changing the legacy `Audio_To_Bits` path.
- Add true four-level audio FLEX candidate decoding for phases B and D while
  retaining the serial four-level slicer.
- Add candidate identity, confidence, deduplication, and conflict retention at
  the message boundary.
- Add soft-decision BCH/FEC experiments only behind comparison gates; the
  current hard-decision parser remains authoritative until fixtures prove an
  improvement without regressions.

Implemented safe portion: adaptive DC/envelope tracking runs beside the legacy
slicer. Four-level audio FLEX preserves legacy A/C phase bits and supplies
enhanced B/D phase bits. The hard-decision protocol parser and every serial
path remain authoritative. A second full decoder and soft FEC remain gated on
representative licensed/redacted fixtures rather than being enabled on faith.

## Phase 6: direct radio inputs

- Add `rtl_tcp` input first, then optional in-process `librtlsdr` support.
- Consider SoapySDR only after the narrower RTL path is stable.
- Isolate optional driver failures so PDW still starts and all legacy inputs
  remain available.
- Add WAV and SigMF capture/replay for reproducible diagnostics and testing.

Implemented in 4.1.0 Beta: reconnecting `rtl_tcp`, a dynamically loaded
receiver catalogue, bundled standard RTL2832U/V3/V4/V4L support, connected
device enumeration, validated custom 32-bit librtlsdr package import,
normalized WAV/SigMF recording and replay, adjustable NFM bandwidth, live
waveform/quality/error diagnostics, and recording-based calibration across
all 1,000 legacy custom slicer combinations. Prior input is restored
automatically and the default remains the legacy local source. All inputs enter
the same in-process decoder boundary; no helper service or separate PDW edition
is created.

## Phase 7: publishing and web integration

- Keep publishing disabled by default and require a first-enable acknowledgement
  that the user is responsible for permissions in their jurisdiction.
- Add generic HTTPS webhooks and static JSON/RSS/Atom/HTML output inside PDW.
- Reuse secure FTP/FTPS/SFTP uploads and Apprise notifications, and consider
  optional MQTT/JSONL profiles.
- Keep decoded source text immutable. Apply aliases, field mapping, templates,
  deduplication, and optional masking only to the published representation.
- Use an asynchronous persistent queue with retry/backoff, event IDs, delivery
  status, pause controls, and a dead-letter view. Publishing failures must never
  block capture or decoding.
- Store credentials in Windows Credential Manager; support HTTPS validation,
  bearer/API tokens, HMAC signing, batching, and rate limiting.

Implemented in the v4 beta integration branch: opt-in filtered/all-message profiles, jurisdiction
acknowledgement, published-copy aliasing/address masking/message omission,
responsive HTML plus JSON/JSONL/RSS/Atom, generic HTTPS JSON webhook delivery,
Windows Credential Manager bearer/HMAC secrets, certificate verification,
idempotency event IDs, rate limiting, pause, bounded retry/backoff, persistent
queue files, and DeadLetter retention. Optional isolated data-output adapters
now add MQTT, SQLite, MySQL through Windows ODBC, a loopback-first read-only
Telnet JSON stream, and native Windows notifications. MQTT remains QoS 0 with
no retained publish; FLEX fragment/group batching is a separate compatibility-
gated enhancement and is not required to publish a website or webhook.

## Release gate for 4.0

A 3.3 stable build should require:

1. A successful clean Visual Studio 2022 Win32 Release build.
2. Startup and shutdown tests on current Windows.
3. Audio-device capture smoke testing.
4. Representative POCSAG and FLEX regression recordings at minimum.
5. Confirmation that existing configuration and filter files round-trip.
6. Review of every release artifact for bundled private logs or traffic data.
7. FTP, FTPS, and SFTP upload smoke tests against disposable test accounts,
   including deliberate certificate and SSH host-key mismatch failures.
