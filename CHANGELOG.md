# Changelog

## PDW v5.5.1 2026 Release

**Public Beta 2 - 11 August 2026**

An additive Capcode filtering and output-routing release. Public Beta 2 retains
the complete Public Beta 1 local-audio profile and compatibility boundary while
giving each directory rule explicit, independently gated behavior.

### Changed

- Consolidate duplicate Settings cards: Filters and Signal & radio now expose
  one card per underlying editor, and Email plus push/Windows notifications are
  grouped under Data outputs.
- When optional FLEX fragment reassembly is enabled, valid fragments are held
  until one ordinary assembled event can be displayed, filtered, logged and
  routed without a synthetic joined-message label.
- Automatically join bounded pager text marked `Part X of Y` for the same
  capcode/protocol/type, including reordered parts, while rejecting ambiguous
  or conflicting chains instead of guessing. Reconstructed repeats of an
  already visible identical message are discarded across capcodes.

### Added

- Independent **Filter** behavior for the lower Filtered pane and **Send to
  enabled outputs** routing for Email, Apprise, Publishing, MQTT, SQLite,
  MySQL/ODBC, Telnet, and Windows notifications.
- Per-rule multi-output selection without bypassing the corresponding global
  Settings enable and configuration gate.
- Agency/service placement before, after, or outside the unified Display name,
  with routed metadata retained even when the visual label is hidden.
- Required-keyword expressions using `+`, validation and helper text, while
  preserving legacy exact, prefix, `&`, POCSAG FNU, and wildcard behavior.

### Compatibility and security

- Dormant legacy rows remain dormant through database migration, CSV backup,
  restore, and hit-counter reset; legacy global output behavior remains until
  an operator deliberately saves explicit routes.
- Specific and exact rules are evaluated before broad capcode-only rules so a
  general directory row cannot shadow a routed keyword or POCSAG FNU match.
- Reject rules take priority over display and output rules, making a match a
  hard discard before either panel, logging, history, notifications or outputs.
- Restore both message panes from their retained buffers after screen/session
  return, display changes and power resume instead of waiting for a scroll.
- Output workers recheck current global enable/acknowledgement state before
  delivery, including work that was already queued.
- Native x64 and Win32 builds, 31 tests per architecture, CodeQL,
  package/source-tamper checks, Defender-scanned Setup, and the complete
  install/upgrade/uninstall matrix gate the exact Public Beta 2 commit.
- The release remains intentionally unsigned and hardware-unverified. Trusted
  Authenticode signing, physical radio/output acceptance, and the broader
  Windows UI/device matrix remain required before stable promotion.

## PDW v5.5 2026 Release

**Public Beta 1 - 11 August 2026**

An additive local-audio profile release. Existing protocol decoders, signal
sources, Capcode Directory rules, filter behavior, message history, display,
logging, notifications, and optional outputs remain available with their
established semantics.

### Added

- An explicit clean-install **SDR# + VB-Audio Cable (Adelaide FLEX)** choice
  beside the standard PDW settings. The profile records the intended decoder
  and local-audio settings without installing or configuring either external
  product.
- Stable Windows audio-endpoint identity for named local-input profiles. PDW
  resolves the configured friendly name only to establish an exact endpoint,
  persists the opaque Windows endpoint ID before capture, and opens that same
  exact ID through WASAPI from the start rather than converting it to a mutable
  WinMM ordinal or selecting the default recording device.
- An explicit, default-No in-app Adelaide FLEX apply action for existing and
  portable installs. It previews the complete known-good local-input, decoder,
  and Custom-slicer reset, probes the exact endpoint before changing settings,
  and creates a verified same-folder byte-exact backup. There is no automatic
  startup prompt or generic-profile migration.
- Explicit Visual Studio 2026/MSVC v145 release builds for both x64 and Win32,
  with the selected compiler version recorded in each dependency lock marker
  and both optional device-smoke programs compile-checked in CI.

### Compatibility and security

- Existing `PDW.INI` files always take precedence during upgrade. The profile
  does not create, replace, import, or modify Capcode Directory entries or
  `filters.ini`.
- A missing or changed saved endpoint fails closed and asks the operator to
  choose an input; PDW does not silently capture the default microphone.
- SDR# and VB-CABLE remain external, operator-installed products. PDW does not
  download, bundle, license, tune, or configure them, and no network or data
  output is enabled by the profile.
- Exact-head Visual Studio 2026 x64 and Win32 builds, all automated tests,
  device-smoke compilation, portable-package/source-tamper audits, combined
  Setup/profile/upgrade/uninstall smoke, Defender scanning, and CI/CodeQL pass.
- The maintainer has explicitly approved an unsigned public prerelease so users
  can perform the remaining physical SDR, SDR#, VB-CABLE and broader Windows UI
  testing. Those paths are beta and hardware-unverified; trusted Authenticode
  signing and post-signing validation remain mandatory before a stable release.
- Testers should report content-free results through GitHub Issues without
  attaching pager traffic, recordings, credentials, endpoint IDs or settings.

## PDW v5.4 2026 Release

An additive FLEX fragment and display-layout release. Existing protocol
decoders, original fragment content and routing, signal sources, filters, and
external outputs remain available with their established semantics.

### Added

- Optional joining of complete standard FLEX alpha and secure fragment chains,
  while every original fragment continues through the established display,
  filter, duplicate, logging, notification, and output path first.
- Bounded, timeout-controlled fragment state keyed by capcode, message number,
  and message type, with normal, reordered, wrapped, replayed, conflicting,
  and truncated cases covered by synthetic regression tests.
- Message display wrapping that uses the available monitor width and aligns
  continuation text beneath the message column without truncating routed text.

### Compatibility and security

- Joining is disabled by default, excluded from FLEX Group Mode, reset with
  decoder/input changes, and never suppresses or delays an original fragment.
- Expected `0`, `1`, `2` positions may wrap repeatedly. Ambiguous cross-cycle
  out-of-order data fails closed, and exact completed-chain replays inside the
  bounded retention window do not emit an extra assembled copy.
- Reassembly remains memory-bounded and emits no private traffic in diagnostics
  or fixtures; live-radio validation requires authorised synthetic, redacted,
  or licensed traffic.

## PDW v5.3 2026 Release

An additive Capcode Directory filtering release. Existing decoders, protocols,
signal sources, archive retention, and output behaviour remain unchanged.

### Added

- Capcode Directory now owns live filtering as well as address descriptions. It exposes legacy
  reject, exact-text, label, monitor-only, command, separate-file and hit-counter behavior in one
  screen, together with the existing global filter options.
- Message text can require multiple case-insensitive words using `+`, for example
  `PR1+Traffic`; every term must occur, in any order. Existing `&` rules remain supported.
- Expanded Capcode Directory CSV import/export carries all filter-rule fields and hit state.

### Compatibility and security

- Fresh packages retire `filters.ini`. Existing files are merged once without duplicating equivalent
  directory mappings, the known duplicated label/message-text generator error is repaired, and the
  source is retained as a `.migrated` backup. Old encrypted configuration backups remain restorable.
- Directory saves, deletes, imports and reloads rebuild the proven legacy runtime matcher
  immediately, so scheduled file regeneration and manual filter reloads are no longer needed.

## PDW v5.2 2026 Release

An additive Message History export release. Existing decoders, protocols,
filters, signal sources, archive retention, and output behaviour remain
unchanged.

### Added

- **Export CSV...** in Message History writes every row matching the current
  Search, Protocol, and Filtered controls, rather than only the visible page.
- UTF-8 spreadsheet-compatible output with the Received, Protocol, Capcode,
  Name, Agency, Type, Message, and Filter headings, including current capcode
  aliases and complete multiline values.

### Compatibility and security

- Export uses an independently hardened read-only SQLite snapshot so active
  history capture can continue while all matching rows are streamed.
- Spreadsheet formulas and leading control characters are emitted as protected
  text; embedded NUL data is rejected instead of producing an ambiguous file.
- Files are written through a same-folder temporary file and atomically
  replaced only after success. The archive database and its SQLite sidecars
  cannot be selected as export destinations.
- Exported CSV files are unencrypted copies outside PDW retention and purge
  controls and must be handled according to operator privacy requirements.

## PDW v5.1 2026 Release

An additive local-operations release for capcode identification, searchable
history, browser-based monitoring, signal diagnostics, and isolated receiver
workers. Existing decoders, protocols, filters, sources, and output behavior
remain available and unchanged by default.

### Added

- A searchable local Capcode Directory that preserves the raw decoded address
  while attaching protocol-specific display names, agencies, colours, notes,
  enabled state, and CSV import/export metadata.
- Optional bounded SQLite message history and a GET-only live dashboard/API
  restricted to `127.0.0.1`; both are disabled by default and message text is
  excluded from history by default.
- Live audio-spectrum and rolling-waterfall diagnostics derived from the same
  captured samples as the existing waveform without changing decoder input.
- An optional RTL polyphase signal conditioner with an exact legacy bypass;
  it is disabled by default and covered by bypass and processing tests.
- Guarded multi-channel receivers using up to four isolated PDW worker
  processes and distinct rtl_tcp endpoints or direct RTL-SDR devices.

### Compatibility and security

- Multi-channel workers preserve decoder isolation, cannot reuse a receiver,
  cannot overwrite the main settings, and disable network/publishing outputs.
  Endpoint matching canonicalizes local and case-equivalent hosts without DNS;
  workers start suspended inside kill-on-close jobs and use a bounded forced
  shutdown fallback. This release does not split one wideband IQ stream into
  several channels.
- Publishing/data-output address masking also removes capcode aliases and
  agency metadata so local names cannot defeat the privacy control.
- Operator-selected archive files must pass checked SQLite connection defenses
  and a first-on-open bounded integrity check before any schema or message SQL.
- The guided upgrade removes only the exact renamed v5 predecessor executable;
  settings, filters, receiver additions, sounds, logs, and databases remain
  preserved.
- The directory, archive, dashboard, multi-channel manager, and signal
  conditioner are opt-in local features. Offline decoder-comparison tooling is
  intentionally not included.

## PDW v5 2026 Release

The first non-beta release of the modernized PDW line. It retains the complete
legacy decoder, receiver, slicer, filter, audio, serial, display and output
surface while introducing a guided Windows installation and upgrade path.

### Added

- One Windows Setup program containing architecture-matched Win32 and x64 PDW
  application choices, with Win32 retained for legacy receiver libraries.
- Guided installation, existing portable-folder detection, configuration and
  filter migration, Start Menu and optional Desktop shortcuts, and safe
  upgrade/uninstall behavior.
- Release automation for dual-architecture installer inputs, installer audit,
  Microsoft Defender scanning, Authenticode verification, and signing gates.

### Compatibility and security

- Portable use remains supported and uses the same executable and beside-the-
  application configuration behavior as previous PDW versions.
- Setup is per-user and does not require administrator rights. It does not move
  credentials out of Windows Credential Manager or enable any network output.
- Public release remains blocked until the application and Setup signatures
  validate against a trusted publisher and the extracted installation passes
  the current Microsoft Defender scan.

## 4.6.1 Beta

Maintenance release establishing enforceable repository and release rules for
the dual-architecture PDW product. Decoder, protocol, slicer, serial, audio,
receiver, driver, filter, and established output behavior are unchanged.

### Added

- Binding engineering and security policies requiring every release to retain
  Win32/x86 beside x64 and preserve legacy scanner, slicer, driver, receiver,
  and protocol paths.
- A repository release audit that aligns canonical version, executable,
  manifest, workflow artifacts, Readme, changelog, dependency notices, both CI
  targets, prepared-statement SQL safeguards, and the audited file boundary.
- A point-in-time dependency and advisory review for pinned OpenSSL 3.5.7,
  curl 8.21.0, libssh2 1.11.1, Windows SQLite, and operator-managed MySQL ODBC.

### Changed

- Removed obsolete VC6/VS2017 project state, generated resource caches, a
  duplicate v3.1 runtime archive, unused source variants, and unreferenced
  bitmaps. CMake is now the only maintained project definition for x64 and
  Win32.
- Advanced all current product, About, resource, manifest, workflow, package,
  branch, fork, and documentation identity to PDW v4.6.1 Beta.
- Packaging and CI now run the release audit before producing an artifact.
- Stabilized the main command bar's icon-and-label layout using the supported
  Windows mixed-button toolbar styles so labels no longer clip into the message
  column header during startup, resize, restore, or maximize.
- Added a dedicated visible signal-quality percentage beside the Live Input
  waveform, including compact-width presentation, instead of leaving the
  quality value hidden behind the meter.

### Compatibility and security

- No Visual Basic source is present. Any future Visual Basic or runtime
  introduction requires explicit supported-version, licensing, security, and
  dual-architecture review.
- SQLite and MySQL decoded-event fields remain bound parameters; table names
  remain restricted identifiers. Optional outputs remain disabled by default.
- No legacy decoder, protocol, slicer, driver, receiver, WinMM, serial, filter,
  configuration, or delivery path was removed.

## 4.6.0 Beta

Dual-architecture follow-up to the merged 4.5 Beta release. The same decoder,
settings, and output behavior is now built and tested for both x64 and Win32,
while Win32 remains available for older receiver drivers and serial hardware.

### Added

- Native x64 Release builds, tests, CI artifacts, and portable packages beside
  the existing Win32 equivalents.
- Architecture checks for optional receiver DLLs, with clear diagnostics when
  a 32-bit DLL is selected in x64 PDW or a 64-bit DLL is selected in Win32 PDW.
- A maintained Windows architecture guide covering package choice, receiver
  compatibility, and the architecture-neutral `rtl_tcp` path.

### Changed

- Removed pointer-width assumptions in shared UI, message, and signal-source
  paths so the same source compiles cleanly for x64 and Win32.
- Made CI and release packaging architecture-aware without removing any legacy
  decoder, WinMM, serial, slicer, filter, or output path.
- Centralized current executable naming and aligned product, resource,
  manifest, workflow artifact, branch, and package identity to PDW v4.6.0 Beta.
### Compatibility

- Win32 remains the recommended build for legacy x86-only receiver DLLs and
  hardware drivers. The x64 package intentionally excludes x86-only receiver
  binaries and DOS-era VxD support assets.
- `rtl_tcp` remains architecture-neutral because PDW communicates with it over
  TCP rather than loading its receiver driver into the PDW process.
- No decoder algorithm, protocol default, filter boundary, or optional-output
  default changed for this release.

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
  configuration. The Release suite now contains 24 tests.
- An image-approved native Windows command bar, clickable live-input meter,
  quiet status bar, and single-instance modeless Settings Center with search,
  draft Apply/Revert behavior, persistent navigation, and an **About me** page.
- Password-encrypted portable backup and restore for every saved INI setting,
  filter, receiver frequency, username, and supported Credential Manager
  secret. Backup / Restore stays under General; file transfer and web
  publishing now share **Data outputs**, removing the duplicate navigation page.

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
- Reorganized the main menu into File, Monitor, Filters, Outputs, View, and
  Help while preserving command IDs and accelerators. Menus, command controls,
  signal surfaces, and retained dialogs now use the approved Windows light/dark
  palette. Settings typography and full-child resize repainting prevent clipped
  headings, duplicated pixels, and fragmented button labels at compact sizes.
- Made the command bar's 54-pixel icon-and-label geometry authoritative from
  first paint through resize, maximize/restore, DPI, and display changes. The
  common-control content pass is retained for correct text placement, then its
  legacy outer height is overridden before monitor panes are positioned.

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
