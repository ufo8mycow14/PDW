# Changelog

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
