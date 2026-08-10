# PDW product roadmap

Updated: 10 August 2026

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
| Win32 build | Clean local build and fresh-clone GitHub build pass all 18 tests | Keep CI green |
| Windows interface | v4.1 themes, menus, Settings, DPI, and startup layout retained | Light/dark, keyboard, DPI, and small-screen acceptance |
| Legacy decoding | POCSAG, FLEX, ACARS, MOBITEX, and ERMES paths retained | Recording-backed protocol regression suite |
| Windows audio | WinMM retained; WASAPI fallback implemented | Live device-loss and hot-plug acceptance |
| Direct radio | `rtl_tcp` and optional RTL-SDR USB implemented | Multi-device live-radio matrix and recovery tests |
| Recording/diagnostics | WAV/SigMF, waveform, quality, error, and calibration tools implemented | Operator workflow and privacy review |
| Secure transfer | FTP/FTPS/SFTP implemented | Disposable-server success and deliberate failure tests |
| Notifications | SMTP retained; Apprise and Windows notifications added | End-to-end non-private delivery tests |
| Web publishing | Static feeds, HTML, and HTTPS webhook implemented | Queue recovery, rate, and privacy acceptance |
| Optional data outputs | MQTT, SQLite, ODBC/MySQL, Telnet, and Windows notifications complete at `67454a1` | Live disposable-service testing |
| Delivery health | Content-free observer, dialog, history, and alerts complete at `682dfd2` | Runtime visual acceptance across themes and DPI |
| Settings compatibility | Unknown INI keys, sections, comments, BOM, and line endings preserved at `98ff7ad` | Extend round-trip fixtures with future settings |
| FLEX fragments | Additive non-group K/F/C alpha reassembly complete at `77e23bd`; original fragments remain authoritative | Recording-backed live acceptance; Group Mode remains legacy |
| Release packaging | v4.5 folder/ZIP audited; fork branch, draft PR, CI, and artifact verified; v4.1 package retained | Manual acceptance before wider release |
| x64 | Not started | Isolate legacy serial and slicer dependencies first |

## Safe integration sequence

The active release branch is `pdw-v4.5.0-beta`. The v4.1 interface, defaults,
and legacy behavior remain authoritative. The `spiral` remote is fetch-only;
work is selectively adopted and independently tested rather than wholesale
merged.

| Order | Stage | State |
| --- | --- | --- |
| 1 | Decoder, buffer, audio, serial, COM, shutdown, SMTP, and configuration safety | Complete at `cffbaea` |
| 2 | One additive decoded-message router after the legacy filter boundary | Complete at `cffbaea` |
| 3 | Isolated MQTT, SQLite, ODBC/MySQL, Telnet, and Windows notification adapters | Complete at `67454a1` |
| 4 | Content-free Delivery Health observer and UI | Complete at `682dfd2` |
| 5 | INI preservation and compatibility verification | Complete at `98ff7ad` |
| 6 | Optional FLEX fragment assembly with guaranteed legacy fallback | Complete at `77e23bd`; disabled by default |
| 7 | PDW v4.5.0 Beta metadata, package, fork, CI, and artifact alignment | Complete; draft PR #5 |

Delivery Health stores no pager addresses or decoded text and cannot alter a
delivery result. FLEX shadow assembly cannot suppress a legacy fragment on
success, timeout, sequence error, or capacity exhaustion. FLEX Group Mode was
not changed without representative replay evidence.

## Milestone 1 - Release-state alignment

Status: complete for local packaging and draft-fork publication

- Align `Headers/version.h`, executable output name, About/resource metadata,
  changelog, documentation, workflow artifact, branch, and package filename to
  **PDW v4.5.0 Beta**.
- Keep Git, local build output, portable package, test installation, pushed
  branch, pull request, CI result, and release artifact as separate states.
- Generate a portable package containing only required runtime files and
  reviewed documentation, with no secrets, traffic, queues, logs, or
  operator-specific settings.
- Retain the prior v4.1 package as a rollback reference rather than overwriting
  it.

Completion gate: a fresh clone builds the intended version, all tests pass,
and source, executable metadata, package contents, GitHub branch, and artifact
name agree.

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

## Milestone 6 - Maintainability and x64 evaluation

Priority: after regression coverage

- Replace unsafe string handling at externally influenced boundaries.
- Continue separating capture, decoder, formatting, output, and presentation
  code behind tested interfaces.
- Add content-safe diagnostics that exclude decoded text by default.
- Isolate legacy serial/slicer requirements, then evaluate an x64 build while
  keeping Win32 available for required hardware.

## Release gates

Every beta or stable release requires:

1. Clean Win32 Release build and all automated tests passing.
2. Startup, shutdown, configuration round-trip, and Windows auto-start checks.
3. Live input smoke testing appropriate to the release scope.
4. Decoder regression evidence for every changed decoder boundary.
5. Security failure tests for changed network or credential features.
6. Visual acceptance at supported themes and scaling levels.
7. Artifact review excluding credentials, private traffic, logs, queues, and
   operator-specific configuration.
8. Matching source version, executable metadata, documentation, branch/tag,
   package, and workflow artifact filename.
