# PDW project handover

Last synchronized: 10 August 2026

Read this file before changing code, replacing an operator installation, or
publishing a release. The central rule is unchanged: PDW remains one program,
the current v4.1 layout and legacy behavior stay authoritative, and enhancements
must be additive or fail independently.

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
themes, Settings hub, receiver workflow, and dialog structure remain the base.

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

The clean Win32 Release build completed on 10 August 2026 with Visual Studio
2022 and the pinned x86 dependency set. The expanded local CTest suite passes
**23 of 23 tests**.

Verified executable metadata:

- filename: `PDW v4.5.0 Beta.exe`;
- file version: `4.5.0.0`;
- product version: `4.5.0 Beta`;
- SHA-256: `D947E13539EF8748737B5CA09561FC89506CD4348ED080F9AFEBEC5DEFE2ABC1`.

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

Full click-through visual automation was unavailable in the active Codex
runtime. Light/dark, keyboard, DPI, and physical receiver acceptance therefore
remain explicit manual gates; a successful build is not presented as that
acceptance. The repeatable matrix is in `docs/WINDOWS_UI_ACCEPTANCE.md`.

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

The local package audit verified all 287 recorded file hashes, found no private
runtime artifacts or old-version filenames, confirmed every optional `Enable`
default and FLEX assembly are off, and confirmed the standard RTL-SDR DLL is in
`Receivers\RTL-SDR`. The packaged executable metadata and SHA-256 match the
tested local build.

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

1. Perform manual System/Light/Dark, keyboard, high-contrast, and 100-200% DPI
   checks, including Screen Options, Data Outputs, and Delivery Health.
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
