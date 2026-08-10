# PDW project handover

Last synchronized: 10 August 2026

Read this file before changing code, replacing an operator installation, or
publishing a release. The central rule is unchanged: PDW remains one program,
the approved 2026 native-Windows navigation design and legacy decoder behavior
stay authoritative, and enhancements must be additive or fail independently.

## Repository and release identity

- Source checkout: `C:\PDW Update\PDW-source`
- Release branch: `pdw-v4.5.0-beta`
- Product/display name: **PDW v4.5.0 Beta**
- Executable: `out\build-win32\Release\PDW v4.5.0 Beta.exe`
- Windows file version: `4.5.0.0`
- Writable fork remote: `fork` (`ufo8mycow14/PDW`)
- Authoritative upstream: `origin` (`Discriminator/PDW`)
- Audit-only comparison remote: `spiral`; push is disabled
- Active task: `019fe6ff-3b3c-7cb2-9cb6-c4c09eb0c71e`

Important integration checkpoints:

- `cffbaea` - decoder/input/output hardening and the unified additive router;
- `67454a1` - isolated optional data outputs;
- `682dfd2` - content-free Delivery Health monitoring;
- `98ff7ad` - INI preservation;
- `77e23bd` - legacy-safe FLEX fragment reassembly.

Fork publication state:

- pushed branch: `fork/pdw-v4.5.0-beta`;
- draft PR: `https://github.com/ufo8mycow14/PDW/pull/5`;
- superseded v4.1 draft PR #3 is closed, with its branch retained;
- Win32 workflow run `31361744748` passed dependency build, application build,
  all 18 tests, and artifact upload;
- CodeQL C/C++, Actions, and final CodeQL checks passed;
- uploaded artifact: `PDW-v4.5.0-Beta-Win32`;
- downloaded artifact copy:
  `C:\PDW Update\output\PDW-v4.5.0-Beta-GitHub-Artifact`.

GitHub's separate AI-findings helper failed before reviewing code because its
configured model was unsupported (HTTP 400). This was a GitHub service-side
agent failure; the standard CodeQL and project checks passed.

Historical v4.1 references in its changelog section and contributor credit are
intentional. Current executable, workflow artifact, documentation, branch, and
package references use v4.5.0 Beta.

## Safe integration result

The requested order was followed:

1. Harden corrupt-frame bounds, audio/serial/COM cleanup, shutdown, SMTP, and
   configuration handling without replacing valid decoder output.
2. Add one decoded-message router after PDW's established filtering boundary.
3. Add MQTT, SQLite, MySQL through Windows ODBC, loopback-first Telnet JSON,
   and Windows notifications as disabled-by-default independent adapters.
4. Add content-free delivery counters/history and non-focus-stealing alerts.
5. Preserve comments, unknown INI keys/sections, BOM, and line endings while
   continuing to update PDW-managed values.
6. Add FLEX fragment assembly last, disabled by default, as a bounded shadow
   observer that never suppresses the legacy fragments.

No wholesale source or UI merge was taken from Spiral. The current PDW menus,
themes, receiver workflow, and dialog implementations remain the compatibility
base. The approved 2026 command bar, modeless Settings Center, and live-input
surface now provide the primary navigation over those retained commands.

## Approved 2026 Windows interface

The image-approved navigation redesign is implemented in the current working
tree. It includes:

- owner-drawn File, Monitor, Filters, Outputs, View, and Help menus that follow
  the selected Windows light/dark palette;
- a 54-pixel command bar with Source, Pause, Record, Filters, Clear, and
  Settings icons and labels; its common-control content layout runs before the
  first show and PDW immediately reasserts the shared 54-pixel outer height so
  startup, resize, DPI, and remote-display changes cannot clip the labels into
  the monitor-column header;
- a clickable two-line **LIVE INPUT** meter driven by real signal diagnostics,
  with 12 history bars and a peak column;
- one persistent modeless Settings window with search, draft retention,
  Apply/Revert controls, 10 navigation destinations, and an **About me** entry
  beneath **Health & diagnostics**;
- **Backup / Restore** under General, producing a portable password-encrypted
  `.pdwbackup` containing PDW.INI, filters.ini, and supported PDW Credential
  Manager records;
- FTP/FTPS/SFTP and web publishing consolidated with MQTT, databases, Telnet,
  and related integrations under **Data outputs**;
- a live signal preview on **Signal & radio**, with retained legacy dialogs
  opened from clear cards without closing Settings; and
- compact-width relayout and full child repainting so moved headings, buttons,
  cards, and meter pixels do not leave resize trails.

The implementation changes presentation and command discovery only. Existing
command IDs and the underlying decoder, capture, filter, configuration, and
delivery implementations remain in place.

## FLEX compatibility boundary

**Settings > Display and behavior > Screen and columns** contains one added row:
**Also show assembled FLEX alpha copy (original fragments remain)**.

The option is off by default. Each fragment is displayed, filtered, logged,
and routed through the original path first. A strict, complete non-group alpha
or secure K/F/C chain can add one marked assembled copy. Orphans, gaps,
out-of-order fragments, timeouts, capacity exhaustion, and truncation cannot
remove the original fragment. Pending state is transient and cleared on decoder
reset, option disable, or Group Mode activation.

FLEX Group Mode is deliberately unchanged because its assignment, missed-call,
conversion, duplicate, and logging state needs representative replay fixtures
before safe modification. See `docs/FLEX_FRAGMENTS.md`.

## Verified local build state

The Win32 Release build completed on 10 August 2026 with Visual Studio 2022 and
the pinned x86 dependency set. The expanded local CTest suite passes
**24 of 24 tests**.

Verified executable metadata:

- filename: `PDW v4.5.0 Beta.exe`;
- file version: `4.5.0.0`;
- product version: `4.5.0 Beta`;
- SHA-256: `E2602F6498401AB20AA5D1724F8F3CAD46ABAF78ADE93CFFFBF94B2D09757D8C`.

A real startup smoke test created a main window titled **PDW v4.5.0 Beta**.
The app remained running until the exact smoke-test process was stopped. The
native resource compiler also accepted the expanded Screen Options dialog.

Follow-up acceptance on 10 August 2026 measured responsive manual main-window
starts from 243 ms to 1,189 ms, including 422 ms after the final clean build,
and responsive `/startup` starts from 5,141 ms to 6,906 ms. The optional hardware smoke targets
captured 44,100 bytes through legacy WinMM at 44.1 kHz 8-bit mono and 48,000
samples through WASAPI at 48 kHz on the final clean-build rerun. See
`docs/LIVE_INPUT_ACCEPTANCE.md`; device removal, hot-plug, `rtl_tcp`, and
physical RTL-SDR tests remain separate open gates.

Native-window UI smoke automation verified single-instance Settings behavior,
General/Appearance/Signal navigation, live-meter routing to Signal & radio,
legacy modal handoff and recovery, explicit Light and Windows-following Dark
rendering, 1000x720 layout, 820x600 compact relayout, and the 720x560 minimum.
The merged backup dialog smoke also verified all 10 navigation destinations,
an unclipped 769x440 dark dialog, and correct disable/re-enable modality.
The approved captures are under `out\ui-*.png`. Physical receiver behavior,
keyboard-only completion, High Contrast, and 125-200% DPI acceptance remain
explicit gates; a successful build is not presented as those results. The
repeatable matrix is in `docs/WINDOWS_UI_ACCEPTANCE.md`.

The continued roadmap pass added synthetic POCSAG alpha, numeric, and tone-only
fixtures around the unchanged legacy decoder; version-2 per-target publishing
state with frozen static folders, monotonic crash recovery, durable feed
history, and restart-unique IDs; release default/privacy tests; keyboard
tab-order fixes; High Contrast recovery; current/legacy volume routing;
owner-monitor dialog clamping; developer-build F1 help; and DPI-scaled Delivery
Health columns. These are additive safety and evidence changes; no protocol
algorithm or legacy input/output was removed.

Rebuild commands:

```powershell
.\scripts\build-dependencies.ps1
cmake -S . -B out\build-win32 -A Win32
cmake --build out\build-win32 --config Release --target clean
cmake --build out\build-win32 --config Release --parallel
ctest --test-dir out\build-win32 -C Release --output-on-failure

# Optional, machine-specific live audio checks
cmake --build out\build-win32 --config Release --target PDWWinmmDeviceSmoke
.\out\build-win32\Release\PDWWinmmDeviceSmoke.exe
cmake --build out\build-win32 --config Release --target PDWWasapiDeviceSmoke
.\out\build-win32\Release\PDWWasapiDeviceSmoke.exe
```

## Portable release package

The reproducible package command is:

```powershell
.\scripts\package-release.ps1
```

It requires a clean Git tree and matching executable metadata, refuses to
overwrite an existing release, stages under `C:\PDW Update\tmp`, and produces:

- `C:\PDW Update\PDW-4.5.0-Beta`;
- `C:\PDW Update\PDW-4.5.0-Beta-Win32.zip`.

The ready-to-run root and `Application` copy include the executable, sanitized
disabled-by-default `PDW.INI`, empty filters, documentation, standard receiver
folder, available legacy support assets, and WAV alerts. `Source` is generated
from `git archive HEAD`. `SHA256SUMS.txt` covers every staged file. The script
rejects runtime logs, databases, IQ/SigMF data, publish queues, dead-letter
content, and non-empty INI secret fields.

The prior `C:\PDW Update\PDW-4.1.0-Beta` folder is retained as rollback evidence
and is not overwritten by the v4.5 package.

The last clean-tree package audit verified all 287 recorded file hashes, found
no private runtime artifacts or old-version filenames, confirmed every optional
`Enable` default and FLEX assembly are off, and confirmed the standard RTL-SDR
DLL is in `Receivers\RTL-SDR`. That portable folder/ZIP predates the uncommitted
2026 interface work and must be regenerated after the interface changes are
reviewed and committed. The Desktop test installation is a separate local
state and receives the current executable without replacing operator INI,
filters, recordings, queues, logs, or monitoring data.

## Desktop live-radio test installation

The current local UI build was merged into the operator's preserved Desktop
test installation on 10 August 2026. The root and
`Application` executable copies both have SHA-256
`E2602F6498401AB20AA5D1724F8F3CAD46ABAF78ADE93CFFFBF94B2D09757D8C`.
Static documentation, receiver support, help, notices, and WAV assets were
refreshed. Existing `PDW.INI`, `filters.ini`, receiver additions, recordings,
logs, queues, and monitoring data were deliberately preserved.
The installation's 291-path static-file manifest was recalculated
after the merge and verified with zero hash mismatches; private runtime extras
were not added to that manifest.

A smoke launch from that exact Desktop path produced a responsive
**PDW v4.5.0 Beta** main window with the `PDWLiveSignalMeter` child and exited
gracefully. This proves installation/startup only; a physical live-radio
transmission and peak response still require operator acceptance.

## Compatibility and privacy boundaries

- Preserve POCSAG, FLEX, ACARS, MOBITEX, and ERMES legacy protocol behavior.
- Keep WinMM, WASAPI fallback, serial slicers, `.rec` playback, INI files,
  filters, logs, WAV alerts, and existing hard-decision parsers available.
- Keep SMTP, Apprise, FTP/FTPS/SFTP, static publishing, and webhooks intact;
  modern data outputs are additional destinations, not replacements.
- Publishing and every optional adapter remain disabled by default and require
  the operator to consider laws and permissions in their own jurisdiction.
- Never commit or package captured pager traffic, private logs, credentials,
  queues, recordings, databases, or operator-specific configuration.
- Secrets belong in Windows Credential Manager, not source, INI, logs,
  screenshots, packages, or chat.
- Keep Git source, local build, portable package, test installation, pushed
  branch, PR, CI, and GitHub release/artifact status explicitly separate.

## Remaining acceptance work

1. Complete keyboard-only, High Contrast, and 125%, 150%, and 200% DPI checks,
   including Screen Options, Data Outputs, and Delivery Health. Light/Dark and
   compact/minimum Settings layouts have 100% evidence.
2. Verify WinMM/WASAPI loss and recovery, `rtl_tcp`, and supported physical
   RTL-SDR USB devices on intended hardware. Default-device capture through
   both Windows audio paths is now recorded in `docs/LIVE_INPUT_ACCEPTANCE.md`.
3. Extend the synthetic POCSAG alpha/numeric/tone fixtures with audio, FLEX,
   filtering, duplicate, correction, and other protocol evidence;
   specifically exercise complete and broken FLEX chains.
4. Test FTP/FTPS/SFTP, SMTP, Apprise, webhook, MQTT, ODBC, Telnet, and Windows
   notification success/failure paths using disposable non-private data.

The maintained milestone view is in `docs/ROADMAP.md`.

## Git publication workflow

- Inspect the complete diff and preserve unrelated work.
- Stage explicit release files; do not use broad cleanup/reset commands.
- Build and test before committing.
- Push `pdw-v4.5.0-beta` to the writable `fork` remote.
- Open a draft PR against the fork's `master`; close older draft PRs only as
  superseded references, without deleting their rollback branches.
- Treat push, PR, merge, tag, GitHub release, and local installation as distinct
  states and report each honestly.
