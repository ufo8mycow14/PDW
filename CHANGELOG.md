# Changelog

## 4.5.0 Beta

Compatibility-first integration release. The v4.1 layout and legacy decoder,
input, filtering, display, logging, SMTP, Apprise, FTP, publishing, and
hardware paths remain available in one executable.

### Added

- One post-filter decoded-message router used by additive destinations without
  replacing the established display/filter/deduplication boundary.
- Disabled-by-default MQTT, SQLite, MySQL through Windows ODBC, loopback-first
  read-only Telnet JSON, and native Windows notification outputs.
- Content-free Delivery Health counters, bounded history, and non-intrusive
  taskbar/sound alerts. Health records never retain pager addresses or decoded
  message text and cannot alter a destination's result.
- Optional standard FLEX K/F/C alpha-fragment reassembly. Original fragments
  are always processed first; a strict, bounded shadow observer can add one
  marked assembled copy after a complete chain. FLEX Group Mode remains on its
  legacy path.
- Warning-clean unit coverage for optional data outputs, delivery health, INI
  preservation, and FLEX fragment sequence, timeout, capacity, restart, and
  truncation behavior.
- Redistribution-safe raw-symbol POCSAG alpha, numeric, and tone-only fixtures
  that compile the unchanged legacy decoder and check exact address, mode,
  type, bitrate, payload, and clean-codeword error results without captured
  pager traffic.
- Optional, non-CI WinMM and WASAPI device smoke executables plus a maintained
  live-input acceptance matrix.
- CI tests for versioned publishing-job persistence, deterministic delivery
  transitions, restart-unique event IDs, and disabled-by-default release
  configuration. The Release suite now contains 23 tests.

### Changed

- Hardened corrupt-frame bounds, long-address handling, COM/serial cleanup,
  audio device recovery, worker shutdown, SMTP queue/TLS behavior, and filter
  parsing without replacing valid legacy output.
- PDW settings saves now merge generated known values into the existing INI.
  Comments, unknown keys, unknown sections, line endings, and BOMs survive;
  stale managed FTP entries and reset-only known values are still removed.
- GitHub CI now builds every configured Release target before running CTest,
  so newly added suites cannot be silently omitted.
- Product, executable, package, artifact, and release-branch naming is aligned
  to PDW v4.5.0 Beta.
- Publishing now durably records the original event ID, frozen static output
  folder, selected/completed/failed destinations, and independent retry counts
  before enqueue. Pause and restart retain work; completed destinations are not
  repeated; a failed destination cannot consume another destination's retries;
  monotonic crash-state is recovered; duplicate pending IDs are suppressed;
  and legacy version-1 or payload-only jobs remain loadable.
- Static publishing restores its bounded rolling history after restart, repairs
  torn JSONL tails, flushes history and rolling files before recording delivery
  completion, and keeps queued events bound to the folder selected at intake.
- Decoded-event IDs now include a Windows-generated GUID while retaining the
  readable UTC prefix and filename-safe format.
- Added keyboard tab access to five legacy checkboxes, corrected High Contrast
  on-to-off palette refresh, retained the old volume mixer behind a current
  Windows fallback, centered dialogs on their owner monitor, and DPI-scaled
  Delivery Health columns.
- Normal developer builds now copy `PDW.pdf` beside the executable so F1 help
  works outside the portable package. Package review scans every staged INI
  for non-empty secret fields, including indented entries.

### Compatibility

- Every new delivery adapter and FLEX reassembly remain disabled by default.
- WinMM, WASAPI fallback, serial slicers, `.rec` playback, filters, logs, WAV
  alerts, legacy protocols, SMTP, Apprise, FTP/FTPS/SFTP, static publishing,
  and webhooks remain in the same program.
- Failed, incomplete, out-of-order, timed-out, or capacity-limited FLEX
  assemblies never suppress the fragment output that older PDW versions show.
- Existing INI extensions remain intact when settings are saved.

## 4.1.0 Beta

Beta release.

### Added

- Optional `WindowTitle` setting for naming separate PDW installations. The
  name appears in the main window and system-tray tooltip.
- Current Windows and Visual Studio 2022 build instructions.
- A modernization roadmap with explicit decoder-compatibility safeguards.
- Continuous multi-file uploads over FTP, explicit FTPS, implicit FTPS, or
  password-authenticated SFTP, with configurable server, remote folder, mode,
  and interval.
- Windows-trusted certificate verification for FTPS and mandatory SHA-256 SSH
  host-key verification for SFTP.
- Background file snapshots and upload status logging in `FileTransfer.log`.
- Windows Credential Manager storage for the hosting password.
- A checksum-verified dependency build for OpenSSL, curl, and libssh2.
- An **Options > Apprise** modal with an exact **Enable Apprise** control,
  authenticated HTTPS API settings, masked Credential Manager persistence,
  sanitized delivery status, and a background test-notification action.
- Provider-neutral notification events and filtered-only Apprise routing while
  retaining SMTP/email as an independently configurable channel.
- Notification routing, privacy-body, payload, severity, and retry unit tests.
- A task-oriented six-menu layout, text toolbar, grouped Settings hub, and
  System/Light/Dark appearance modes.
- Current Windows Common Controls styling and per-monitor DPI awareness.
- Updated About metadata crediting Kieran O'Rourke for the v4.1.0 beta work.
- A **Start PDW with Windows** setting. Automatic sign-in launches wait five
  seconds so Windows can finish loading audio devices and other services;
  manual launches remain immediate.
- Event-driven WASAPI capture as an automatic fallback when the retained WinMM
  input cannot open the selected legacy format.
- Reconnecting `rtl_tcp` input and optional in-process RTL-SDR USB input via a
  dynamically loaded vendor DLL; neither is required for PDW to start.
- Operator WAV/SigMF signal recording and replay with automatic restoration of
  the previous audio, radio, or serial source.
- Adaptive DC/envelope tracking and hybrid four-level FLEX slicing. Existing
  A/C phase decisions stay on the legacy sign path while enhanced inner/outer
  decisions add B/D phase capability.
- Opt-in web publishing with a jurisdiction acknowledgement, JSON, JSONL, RSS,
  Atom, responsive HTML, generic HTTPS webhooks, bearer/HMAC authentication,
  published-copy privacy transforms, asynchronous retry, persistent queueing,
  pause/rate controls, and DeadLetter retention.
- Synthetic signal, recording, radio-demodulation, candidate-consolidation,
  capture-conversion, and publishing-format tests.
- A portable receiver catalogue with a bundled 32-bit RTL-SDR standard pack,
  connected-device names, legacy side-by-side DLL fallback, safe custom
  librtlsdr package import, and an optional WinUSB setup tool kept in the same
  `Receivers` directory.
- Adjustable 5-25 kHz NFM low-pass bandwidth for direct and RTL-TCP inputs.
- Live waveform, level/noise/clipping/eye metrics, quality history, FLEX phase
  error totals, and recording-based comparison of all 1,000 custom legacy
  threshold/centering/resync combinations.

### Changed

- Centralized application, resource, and build version metadata.
- Updated product metadata from the Windows XP-era description.
- Modernized the CMake target layout while retaining a Win32 build.
- Updated GitHub Actions to Node.js 24-compatible action versions.
- Replaced the checked-in OpenSSL 3.5.5 bundle with a reproducible OpenSSL
  3.5.7 Win32 build. Added curl 8.21.0 and libssh2 1.11.1 for the secure file
  transfer protocols.
- Normalized touched legacy source files from Windows-1252 to UTF-8.
- Preserved Windows-1252 runtime marker bytes through explicit MSVC source
  and execution character-set settings.
- Routed decoded-message email and push delivery through the notification
  manager without changing the existing SMTP selection modes.
- Reworked dialog sizing and typography to prevent clipped text and keep the
  filter manager inside the current monitor's working area.
- Replaced the fixed-background signal bitmap with scalable, theme-aware
  signal bars and explicit reception-quality states.

### Compatibility

- POCSAG, FLEX, ACARS, MOBITEX, and ERMES decoder algorithms are unchanged.
- Existing `pdw.ini` files remain compatible; `WindowTitle` defaults to empty.
- File transfer is disabled by default. Existing FTP settings remain valid and
  default to the classic FTP protocol. Passwords saved by the preliminary FTP
  implementation remain recognized.
- Apprise is disabled by default. Existing `pdw.ini`, filters, and SMTP/email
  settings remain compatible; Apprise secrets are not stored in the INI file.
- The executable remains 32-bit for legacy hardware compatibility.
- WinMM, serial slicers, `.rec` playback, INI files, filters, logs, WAV alerts,
  and all legacy protocol parsers remain available in the same executable.
- Publishing and radio inputs are disabled or unselected by default. Publishing
  cannot be enabled without the operator's jurisdiction acknowledgement.
- Receiver packages remain dynamically loaded only after selection. The
  default source remains the legacy local input, and every legacy input preset
  remains available after optional recording calibration.

## 3.2b01

- Added SSL support to the SMTP client.
- Last application beta before the 3.3 modernization work.

## 3.12

- Stable open-source release published in July 2013.
