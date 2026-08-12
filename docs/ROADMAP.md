# PDW product roadmap

Updated: 12 August 2026

This is the concise delivery roadmap. `MODERNIZATION.md` contains the detailed
technical strategy, while the root `HANDOVER.md` records exact repository,
build, package, and publication state.

## Guiding outcome

Deliver one dependable Windows PDW application that keeps the proven legacy
decoders, hardware paths, layout, and outputs while adding current Windows
input, diagnostics, secure delivery, and maintainable test boundaries.

## Status map

| Area | Current state | Next gate |
| --- | --- | --- |
| Win32 build | v5.5.2 passes the explicit Visual Studio 2026/MSVC v145 Win32 clean build, 35-test suite, device-smoke compilation, package/source-tamper gates and exact-head CI | Collect physical legacy receiver/slicer feedback without weakening Win32 compatibility |
| Windows interface | Approved 2026 command bar, live meter, modeless 9-page Settings Center, encrypted configuration backup/restore, dark/light palette, compact relayout, and retained dialog routing implemented | Keyboard, High Contrast, 125-200% DPI, and physical-radio acceptance |
| Legacy decoding | Existing protocols retained; synthetic POCSAG alpha, numeric, and tone-only fixtures exercise the unchanged decoder | Add correction, FLEX, recording, filter, duplicate, and other-protocol fixtures |
| Windows audio | WinMM and WASAPI remain available; Public Beta 2 retains stable endpoint IDs and endpoint-specific WASAPI capture for an explicit SDR#/VB-CABLE profile, with automated fail-closed coverage | Physical device-loss, hot-plug and broader device-matrix beta acceptance |
| Direct radio | `rtl_tcp` and optional RTL-SDR USB implemented | Multi-device live-radio matrix and recovery tests |
| Recording/diagnostics | WAV/SigMF, waveform, quality, error, and calibration tools implemented | Operator workflow and privacy review |
| Secure transfer | FTP/FTPS/SFTP implemented | Disposable-server success and deliberate failure tests |
| Notifications | SMTP retained; Apprise and Windows notifications added | End-to-end non-private delivery tests |
| Web publishing | Static feeds/webhook plus v2 restart-safe per-target state, frozen folders, GUID IDs, durable history, partial completion, deduplication, and monotonic recovery tests | Live rate, privacy, and terminal-vs-transient failure acceptance |
| Optional data outputs | MQTT, SQLite, ODBC/MySQL, Telnet, and Windows notifications complete at `67454a1` | Live disposable-service testing |
| Delivery health | Content-free observer, dialog, history, and alerts complete at `682dfd2` | Runtime visual acceptance across themes and DPI |
| Settings compatibility | Unknown INI keys, sections, comments, BOM, and line endings preserved at `98ff7ad` | Extend round-trip fixtures with future settings |
| Pager fragments | Non-group FLEX K/F/C and explicit `Part X of Y` text reassembly are bounded, replay-safe, and emit one joined message; FLEX header joining remains opt-in and Group Mode remains legacy | Recording-backed live acceptance |
| Repository hygiene | File-by-file x64/Win32 audit complete; obsolete VC6/VS2017 state, caches, duplicate archive, and unused code/assets removed; CMake is authoritative | Enforce `scripts/audit-release.ps1` and repeat the review when adding native dependencies or release-only assets |
| Local operations | Capcode Directory with CSV upsert/deduplication, independent lower-panel filtering, explicit multi-output routing, optional bounded history, loopback-only dashboard, one-way Local Gateway Outbox, spectrum/waterfall, isolated multi-channel workers, and optional RTL conditioning are implemented | Complete operator UI and physical multi-receiver/output acceptance without changing decoder behavior |
| Named local-input profile | Public Beta 2 retains the explicit clean-install Adelaide FLEX profile, stable exact endpoint identity, fail-closed capture, default-No verified-backup apply action and passing automated installer/profile smoke; SDR# and VB-CABLE remain external | Collect licensed physical-workflow and dual native-UI feedback without changing operator data |
| Release packaging | One combined `PDW-v5.5.2-2026-Release-Setup.exe` is gated by provenance, profile selection, predecessor cleanup, Defender, trusted signing and the Win32/x64 install/upgrade/uninstall matrix | Publish the normal GitHub release only after every gate passes |
| x64 | PDW v5.5.2 is gated by the Visual Studio 2026 x64 clean build, complete test suite, device-smoke compilation, package/source-tamper gates, combined Setup and exact-head CI | Collect physical receiver and broader Windows UI acceptance while keeping Win32 available |

## Safe integration sequence

The v5.5.2 release state is maintained on fork `master`. The approved
interface, defaults, and legacy behavior remain authoritative. The `spiral` remote is
fetch-only; work is selectively adopted and independently tested rather than
wholesale merged.

| Order | Stage | State |
| --- | --- | --- |
| 1 | Decoder, buffer, audio, serial, COM, shutdown, SMTP, and configuration safety | Complete at `cffbaea` |
| 2 | One additive decoded-message router after the legacy filter boundary | Complete at `cffbaea` |
| 3 | Isolated MQTT, SQLite, ODBC/MySQL, Telnet, and Windows notification adapters | Complete at `67454a1` |
| 4 | Content-free Delivery Health observer and UI | Complete at `682dfd2` |
| 5 | INI preservation and compatibility verification | Complete at `98ff7ad` |
| 6 | Optional FLEX fragment assembly with guaranteed legacy fallback | Complete at `77e23bd`; disabled by default |
| 7 | PDW v4.5.0 Beta metadata, package, fork, CI, and artifact alignment | Complete; PR #5 merged into fork `master` |
| 8 | Image-approved 2026 Windows navigation, live input, and Settings Center | Complete in fork `master`; 100% Light/Dark and compact-size smoke passed |
| 9 | PDW v4.6.0 Beta native x64 plus retained Win32 release alignment | Clean local dual gates and package audit complete; draft PR #6 opened for CI/review |
| 10 | PDW v4.6.1 Beta repository, security, legacy-retention, and release-identity enforcement | Local audit, clean dual builds/tests/smoke, metadata, About, UI and package gates pass; draft PR #7 dual CI and CodeQL checks pass |
| 11 | PDW v5 2026 Release identity and guided dual-architecture Windows installer | Implemented locally; dual install/upgrade/uninstall smoke and Defender scan pass; trusted Authenticode signing remains the public-release gate |
| 12 | PDW v5.1 local capcode, archive, dashboard, diagnostics, multi-channel, and RTL-conditioning tools | Implemented locally with legacy-compatible defaults; dual build/test gates pass and PR installer CI remains the source-merge gate |
| 13 | PDW v5.2 complete Message History CSV export | Implemented with all-filtered-row snapshot export, spreadsheet hardening, atomic destination replacement, and dual-architecture regression coverage; PR CI and manual native UI acceptance remain gates |
| 14 | PDW v5.3 Capcode Directory live filtering | Implemented with legacy-rule migration, immediate runtime matching, expanded CSV fields, multiword `+` conditions, and dual-architecture regression coverage; PR CI and manual native UI acceptance remain gates |
| 15 | PDW v5.4 FLEX fragment joining and wide message layout | Extended from its original shadow path to wait for a valid complete chain, emit one joined event, and support bounded explicit `Part X of Y` sender text; recording-backed acceptance remains a gate |
| 16 | PDW v5.5 explicit SDR# + VB-Audio Cable Adelaide FLEX profile and endpoint-specific WASAPI capture | Public Beta 1 approved: dual builds/tests, Setup/profile/upgrade/uninstall, packages, CI/CodeQL and Defender pass; trusted signing, full native UI and physical SDR#/VB-CABLE acceptance remain open and clearly labelled |
| 17 | PDW v5.5.1 independent Capcode filtering and explicit multi-output routing | Public Beta 2 approved: legacy-safe migration, specific-first matching, dual builds/tests, native Capcode dialog smoke, packages, Setup/upgrade/uninstall, CI/CodeQL and Defender pass; trusted signing and physical output/device acceptance remain open and clearly labelled |
| 18 | PDW v5.5.2 rejected-message discard, multipart presentation, idle-return repaint, Capcode CSV upsert, and Local Gateway Outbox | Feature work and focused dual-architecture regressions pass; exact release-candidate CI, packages, Setup/upgrade/uninstall, Defender, trusted signing and physical/UI acceptance remain publication gates |

Delivery Health stores no pager addresses or decoded text and cannot alter a
delivery result. FLEX shadow assembly cannot suppress a legacy fragment on
success, timeout, sequence error, or capacity exhaustion. FLEX Group Mode was
not changed without representative replay evidence.

## Milestone 1 - Release-state alignment

Status: PDW v5 2026 Release identity and guided installer implemented locally;
dual build, test, audio, UI, and isolated installer gates pass while trusted
Authenticode signing and public publication remain separate states

- Align `Headers/version.h`, executable output name, About/resource metadata,
  changelog, documentation, workflow artifact, branch, and package filename to
  **PDW v5 2026 Release**.
- Keep Git, local build output, portable package, test installation, pushed
  branch, pull request, CI result, and release artifact as separate states.
- Generate one guided Windows installer containing the architecture-matched
  x64 and Win32 applications, plus portable packages for users who prefer the
  established folder-based operation. Exclude secrets, traffic, queues, logs,
  and operator-specific settings from every artifact.
- Retain the prior v4.1 package as a rollback reference rather than overwriting
  it.

Completion gate: a fresh clone builds the intended version, all tests pass,
source and executable metadata agree, installation and portable operation both
pass, and the public installer is Authenticode-signed by the approved publisher.

## Milestone 2 - Live-radio acceptance

Priority: release follow-up

- Verify WinMM and WASAPI against real Windows audio devices.
- Verify input-device loss, reconnection, and restart behavior.
- Test `rtl_tcp` reconnects, tuning changes, and malformed network input.
- Test supported RTL-SDR USB packages without making their DLLs a startup
  dependency.
- Confirm five-second Windows auto-start allows devices to settle while manual
  startup remains immediate.

Completion gate: repeatable notes cover each supported input and PDW returns
safely to a known input after failures and replay.

Current evidence: `docs/LIVE_INPUT_ACCEPTANCE.md` records successful manual and
five-second auto-start timing plus real default-device capture through both the
legacy WinMM format and the WASAPI fallback. Device removal/recovery,
`rtl_tcp`, and physical RTL-SDR acceptance remain open.

## Milestone 3 - Decoder regression evidence

Priority: release blocking for future decoder changes

- Add synthetic, redacted, or redistributable POCSAG and FLEX fixtures first.
- Extend fixtures to ACARS, MOBITEX, and ERMES.
- Record expected text, address, mode, bit-error, filter, and duplicate results.
- Compare legacy and enhanced candidates and preserve disagreements for
  operator review.
- Add licensed recordings for FLEX fragment acceptance before considering any
  change to FLEX Group Mode.

Completion gate: CI detects decoder-output regressions. No private live traffic
is stored in the repository.

Current evidence: deterministic raw-symbol POCSAG alpha, numeric, and tone-only
fixtures compile the unchanged `Pocsag.cpp` and check exact addresses,
functions/modes, legacy types, bitrates, payloads, clean-codeword validation,
and bit-error observations. Production correction, audio/WAV, FLEX, filtering,
duplicate, ACARS, MOBITEX, and ERMES fixtures remain open.

## Milestone 4 - Secure delivery validation

Priority: before broad public use

- Test FTP, explicit FTPS, implicit FTPS, and SFTP with disposable services.
- Verify certificate failure and SSH host-key mismatch are rejected.
- Exercise SMTP, Apprise, webhooks, MQTT, SQLite, ODBC/MySQL, Telnet, and
  Windows notifications with synthetic non-private events.
- Confirm retries, pause, restart recovery, rate limiting, duplicate event IDs,
  and dead-letter behavior do not block capture or decoding.
- Verify credentials never enter INI files, logs, screenshots, or packages.

Completion gate: success and deliberate-failure evidence exists for each
enabled output, while every optional output remains disabled by default.

Current evidence: publishing tests now cover version-2 atomic state, version-1
and payload-only compatibility, restart-unique safe IDs, frozen static paths,
independent per-target retries, selected/completed/failed destinations,
monotonic temporary recovery, torn-history repair, exact terminal thresholds,
pending-ID deduplication, and malformed/oversized input. A separate
release-default test keeps every optional destination off and privacy-sensitive
defaults intact. Live services and terminal/transient network classification
remain open.

## Milestone 5 - Windows usability and accessibility

Priority: before stable release

- Audit every dialog at 100%, 125%, 150%, and 200% display scaling.
- Test System, Light, Dark, high contrast, keyboard navigation, and narrow
  screens.
- Include Radio and replay, Data outputs, Delivery Health, and the added FLEX
  assembly row in visual acceptance.
- Replace obsolete help routes and clipped legacy terminology only where the
  existing workflow remains recognizable.

Completion gate: primary workflows are usable without clipped controls or a
mouse, and failures provide an actionable recovery path.

Current evidence: all visible interactive resource buttons participate in tab
order; High Contrast can return to the selected System/Light/Dark palette;
current Windows volume control retains the legacy fallback; dialogs center and
clamp within their owner's monitor; F1 help is staged in developer builds; and
Delivery Health columns use the active DPI. The image-approved 2026 shell now
adds the six-menu information architecture, six-command icon bar, real-sample
live meter, persistent Settings navigation, 10 destinations including **About
me**, and resize-safe cards. Native-window smoke passed at 100% in Light and
Dark, 1000x720, 820x600, and the 720x560 minimum. The command bar also retains
its full icon labels and 54-pixel separation from Pane 1 on initial paint,
display change, and repeated maximize/restore cycles. Keyboard-only, High
Contrast, 125-200% DPI, and physical-radio acceptance remain open in
`docs/WINDOWS_UI_ACCEPTANCE.md`.

## Milestone 6 - Maintainability and dual-architecture support

Priority: after regression coverage

- Replace unsafe string handling at externally influenced boundaries.
- Continue separating capture, decoder, formatting, output, and presentation
  code behind tested interfaces.
- Add content-safe diagnostics that exclude decoded text by default.
- Keep native x64 and Win32 builds warning-clean at shared pointer and handle
  boundaries, while retaining Win32 for required legacy hardware.
- Validate in-process receiver DLL architecture before load and keep `rtl_tcp`
  available as an architecture-neutral receiver path.
- Keep `docs/REPOSITORY_AUDIT.md` aligned with the CMake targets and portable
  package rules; do not restore generated IDE project/user files.

## Milestone 7 - Guided Windows installation

Priority: current release

- Build one Setup executable containing the x64 and Win32 PDW applications.
- Default to x64 on 64-bit Windows while retaining a clear Win32 compatibility
  choice for legacy x86 receiver DLLs and older hardware.
- Keep `PDW.INI`, filters, receivers, WAV files, logs, and the executable in
  the selected PDW folder; use in-application Backup / Restore to move settings
  from another copy.
- Preserve `PDW.INI`, filters, receiver additions, WAV files, logs, and the
  same-user Windows Credential Manager records during upgrade and uninstall.
- Keep the folder-based portable packages supported and behaviorally identical.
- Require Authenticode signing and a clean Microsoft Defender scan before the
  installer is promoted as the public stable release.

Current evidence: Inno Setup builds the single v5.5.2 installer; isolated x64 and
Win32 install, settings co-location, upgrade-preservation and
uninstall-preservation smoke pass. The application and installer scan clean
with Microsoft Defender. Trusted signing and post-sign validation remain
mandatory before publication.

## Release gates

Every release requires all gates below:

1. Clean x64 and Win32 Release builds and all automated tests passing.
2. Startup, shutdown, configuration round-trip, and Windows auto-start checks.
3. Live input smoke testing appropriate to the release scope.
4. Decoder regression evidence for every changed decoder boundary.
5. Security failure tests for changed network or credential features.
6. Visual acceptance at supported themes and scaling levels.
7. Artifact review excluding credentials, private traffic, logs, queues, and
   operator-specific configuration.
8. Matching source version, executable metadata, documentation, branch/tag,
   package, installer, and workflow artifact filename.
9. Trusted Authenticode signatures on the public installer and installed
   executables, followed by Microsoft Defender scanning and clean-install
   validation on supported Windows architectures.
