# PDW v5.5.3 2026 Release handover

Updated: 16 September 2026

## Active receiver-recovery task

The owner has now authorised diagnosis, repair and local rollout on Lounge,
using Chrome Remote Desktop and the Lounge task `Restore PDW pager decoding`.
The earlier read-only handoff and installation freeze do not prevent that
specifically authorised controlled recovery. This is not authority to publish
a stable release, disclose decoded traffic or change unrelated systems.

The Lounge inspection found a Win32 build based on `ba1464c`, Windows audio
(`AudioSource=0`), an SDRSharp-launching helper and duplicate PDW startup entries.
The September audit changes were not present in that installed build. The Lounge
task backed up its settings and launcher locally before authorised repair.

I reproduced a direct RTL-SDR defect against the bundled V1.4.0 source semantics:
`rtlsdr_set_freq_correction` returns `-2` when the requested PPM is already set.
PDW rejected that harmless result at the default zero PPM. The corrected source
accepts only that specific no-change result and still rejects genuine errors.
I also removed the blocking modern-source startup path by using the existing
silent retry service before the normal timer starts. A native regression proves
recovery after delayed receiver and library availability, including `/startup`.
The old executable fails that regression; v5.5.3 passes the complete 38-test suite
on Win32 (35.45 s) and x64 (36.50 s). The Lounge task independently reproduced
the real zero-PPM error -2 and confirmed a temporary one-PPM setting allows the
old driver to open. It restored zero PPM and stopped PDW for the corrected update.
It also backed up and removed the duplicate Startup shortcut, retained HKCU Run
with `/startup`, and changed the helper to launch PDW without SDRSharp. No valid
accepted rows were established in that preliminary diagnostic.

The OneDrive result created on Lounge is not yet visible on this computer;
delivery and supervision use the already-authorised Chrome Remote Desktop
session. Final clean-commit package and physical acceptance results belong to
the exact resulting candidate, not the historical runs below.

I am preparing the required local source commit and architecture-matched rollout
under the owner's update request. Production data stays local to Lounge, and
rollback copies must be retained. The new development identity is v5.5.3.

## September development follow-up

I resumed from clean `master` commit
`ab0dbb45aada84af16e1088e8f3bf96df3107bb4`. That tree includes the audio-starvation
and audit repairs at `53740d1` and the outbox contention-test follow-up at
`ab0dbb4`. The 7 September review handoff records 37/37 tests on each architecture;
the older 35-test release records below do not describe that later source.

On 16 September I built both architectures from fresh directories. Each baseline
suite exposed the same calendar-dependent archive-manager fixture: its fixed
10 August event was now older than the configured 30-day retention policy. I
changed only the fixture timestamp to current UTC, preserving production retention.

I also prepared the verified OpenSSL 3.5.8 and curl 8.22.0 security source pins;
`docs/DEPENDENCY_SECURITY.md` records the upstream review and the remaining
Connector/ODBC binary/release-note limitation. Dependency build outputs are
isolated under `out/dependencies-20260916/{x64,x86}`; execution logs and downloaded
public review evidence are under `out/validation-20260916`. The source changes
remain uncommitted and retain the v5.5.2 development identity. A published update
must advance and align all release-version surfaces through the normal gates.

The current production receiver is on the Lounge PC, not this build computer.
I prepared a read-only coordination packet at
`OneDrive\Documents\Deployment\PDW\LOUNGE_READ_ONLY_HANDOFF.md`. Creating that file
does not establish sync or start a remote task. No live installation may be
replaced, stopped or reconfigured through that packet. Physical soak, trusted-feed
correlation and independent release approval remain required.

No usable code-signing certificate was found in this computer's CurrentUser or
LocalMachine personal certificate stores. A configured approved remote/hardware
signer has not been established. Packaging and Setup require an authorised clean
source commit; dirty-state provenance must not be relabelled or bypassed.

### Validation completed on 16 September

| Check | x64 | Win32/x86 |
| --- | --- | --- |
| Fresh OpenSSL 3.5.8, libssh2 1.11.1 and curl 8.22.0 dependency build | Passed | Passed |
| Fresh application and complete Release target build | Passed | Passed |
| Complete CTest suite | 37/37; 19.53 s | 37/37; 22.99 s |
| Archive-manager and bounded decoder-worker repeat-until-fail | 20/20 each | 20/20 each |
| Gateway outbox repeat-until-fail | 5/5 | 5/5 |
| Optional WASAPI and WinMM smoke compile and execution | Both passed | Both passed |
| Native startup | Passed; responsive main window and Settings | Passed; responsive five-second hidden process |
| PE/file/product metadata | `0x8664`, `5.5.2.0`, `5.5.2 2026 Release` | `0x014C`, same versions |
| Provenance | Exact base commit, `state=dirty` | Exact base commit, `state=dirty` |

I verified the x64 dark main window, General/Appearance/About Settings navigation,
and About dialog version/architecture text. I did not complete Light/High Contrast,
DPI, compact-window, Win32 visual or graceful-exit acceptance: concurrent desktop
activity made further interactive input unsuitable. I stopped only the isolated
synthetic smoke processes; no operator application was stopped. The device smoke
tests retained counts only and did not record or publish audio. Their success on
this computer does not establish the Lounge receiver's behaviour.

The static release audit, dependency-recipe PowerShell parse check and immutable
provenance smoke passed. The latter uses its own synthetic Git fixture, not a
release package of this tree. The Setup fail-closed harness refused the modified
tree at its clean-source precondition; its deeper installer cases were not run.
No current package, installer, signature or live deployment was produced.

Build directories are `out/validation-20260916-updated-{x64,Win32}`. The earlier
`out/validation-20260916-{x64,Win32}` directories are the failing baseline run.
`out/validation-20260916/native-metadata.json` records these development hashes:

- x64: `D16137E172DE310B8BF54DF5F52344AB64B9BD0CF9BB75521966E146F09A3DD6`
- Win32: `8042A7F13611730CF2A7E39840DBE1EEA60BEC5CB33B09ABC554845F9E1E02B6`

Remaining gates are an authorised reviewed commit followed by clean rebuild,
portable/source-tamper/Setup validation, remaining native UI and graceful-exit
checks, exact-commit CI/CodeQL, independent review, physical receiver/remote-session
acceptance, and approved trusted signing. Commit, push, release and Lounge
installation authority remain separate; this working-tree follow-up does not
authorise any of them.

## Current release identity

- Repository: `C:\PDW Update\PDW-source`
- Active branch: fork `master`
- Candidate tag identity: `v5.5.3` (no tag or public release created)
- Product/display name: **PDW v5.5.3 2026 Release**
- Executable: `PDW v5.5.3 2026 Release.exe`
- Product version: `5.5.3 2026 Release`
- File/manifest version: `5.5.3.0`
- Installer: `PDW-v5.5.3-2026-Release-Setup.exe`
- Portable packages: `PDW-v5.5.3-2026-Release-Win32` and
  `PDW-v5.5.3-2026-Release-x64`

PDW remains one native C++ product with mandatory Win32 and x64 targets. The
guided installer and portable packages are two delivery forms of that same
application; they do not define separate editions or decoder behavior.
The maintained release toolchain is Visual Studio 2026/MSVC v145 on the
`windows-2025-vs2026` hosted image. Visual Studio 2022/v143 remains an explicit
local rollback option, not the v5.5.2 release target. Dependency locks record the
exact compiler, generator, toolset, CMake version and resolver recipe.

`Headers/version.h` is the authoritative current identity. CMake reads the
display name for the executable output, while resources, the manifest,
workflow artifact names, packaging, Setup, the main title, About, and current
documentation must agree with it before release.

## Historical v5.5.2 scope and release procedure

The release retains the complete Public Beta 2 Capcode Directory and explicit
clean-install **SDR# + VB-Audio Cable (Adelaide FLEX)** behavior. It adds
rejected-message hard discard, normal joined-message presentation, idle-return
pane repainting, Capcode CSV upsert/deduplication, and the disabled-by-default
one-way Local Gateway Outbox. Its compatibility boundary remains narrow:

- SDR# and VB-CABLE are external, operator-installed products. PDW does not
  download, bundle, license, tune, or configure them.
- The profile sets only local audio and decoder configuration. It does not
  create, replace, import, or modify Capcode Directory entries or
  `filters.ini`.
- Existing `PDW.INI` always takes precedence during upgrade and reinstall.
- The initial friendly-name match establishes an exact Windows endpoint. PDW
  persists that opaque ID before opening it and then uses endpoint-specific
  WASAPI from the start rather than converting it to a mutable WinMM ordinal.
- If the saved endpoint is missing or has changed, capture fails closed and the
  operator is asked to choose an input. PDW does not silently capture the
  default microphone.
- The clean-install Adelaide file differs from Standard only where the named
  preset needs different packaged defaults. The explicit in-app Apply action is
  intentionally a broader, previewed and reversible reset of the complete
  known-good local-input, decoder and Custom-slicer state; it probes first and
  creates a verified byte-exact PDW.INI backup. It is never automatic.
- No startup prompt, silent migration, or generic-profile conversion is run;
  existing users must choose the explicit default-No Apply action themselves.
- Dormant legacy directory rows remain dormant through migration, backup,
  restore, and hit-counter reset. Existing global routing remains until an
  operator deliberately saves explicit destinations on the rule.
- Selected outputs never enable themselves; every Email, Apprise, Publishing,
  MQTT, SQLite, MySQL/ODBC, Telnet, or Windows destination remains gated by its
  corresponding Settings enable/acknowledgement and configuration.
- The Local Gateway Outbox writes decoder-finalized events to an append-only
  SQLite WAL database on an isolated bounded worker. It contains no cloud or
  network client, does not use per-capcode destination routing, and cannot stop
  decoding if its queue, database, retention, or disk operation fails.

The profile does not enable a network, notification, publishing, database, or
other data-output destination.

## Installer and package boundary

The maintained Setup definition is `installer/PDW.iss`. The v5.5.2 release:

- offer x64 or Win32 on 64-bit Windows and use Win32 on 32-bit Windows;
- keep the application and mutable operator data in one selected PDW folder;
- preserve existing settings, Capcode Directory data, legacy `filters.ini`
  recovery files, receivers, sounds, logs, queues, recordings, and databases;
- offer the named profile only during a genuinely clean installation;
- never install a new `filters.ini` or overwrite an existing `PDW.INI`;
- remove only exact renamed predecessor executables, including v5.5; and
- support trusted signing of the application, Setup, and uninstaller.

Win32 may retain reviewed x86-only receiver assets. x64 must exclude those
binaries and use architecture-matched libraries or the architecture-neutral
`rtl_tcp` path. Portable packaging remains supported and must carry the same
clean-install profiles and documentation without private runtime data.

## Historical 12 August dependency review

The 12 August 2026 review retains the pinned OpenSSL 3.5.7 LTS, curl 8.21.0,
libssh2 1.11.1, Windows `winsqlite3`, and operator-managed MySQL ODBC boundary.
Connector/ODBC 9.7.0 is the current reviewed GA release. PDW does not bundle or
install it; operators use Oracle's architecture-matched driver and a secured
Windows DSN. Inno Setup 7.0.2 is available, but v5.5.2 deliberately retains
pinned 6.7.3 because changing installer-compiler major version during the
profile and upgrade change would broaden release risk; migration to 7 remains
a separate dual-architecture installer project.

The reviewed external-profile references are SDR# production revision 1921 and
VB-CABLE Package 45. Neither is packaged. Operators obtain, secure, support,
and license them independently; VB-CABLE professional use is subject to the
vendor's licensing terms.

## Current verification state

The v5.5.2 automated release gate requires clean Visual Studio 2026/MSVC v145
x64 and Win32 builds, the complete current CTest suite (37 tests), both optional
device-smoke programs, deterministic portable/source-tamper checks, Setup
metadata/architecture and Defender validation, and the complete
standard/profile/install/upgrade/uninstall preservation matrix. The immutable
v5.5.2 tag may be created only after Build/Setup, CodeQL, signing and post-sign
validation pass for the exact clean merged `master` commit used by every published asset.

The following evidence remains open and must not be implied by those automated
results:

- complete native Light/Dark, compact-size, keyboard, High Contrast and DPI
  acceptance on representative x64 and Win32 systems;
- physical SDR, SDR#/VB-CABLE, receiver, slicer, serial, hot-plug and
  device-loss acceptance; and
- trusted Authenticode signing/timestamp plus post-signing validation.

Normal GitHub release publication remains blocked until the application, Setup
and uninstaller have trusted Authenticode signatures and timestamps, the signed
artifacts pass Defender and post-sign validation, and the required acceptance
evidence is complete.

## Rebuild and audit

```powershell
.\scripts\audit-release.ps1

$generator = .\scripts\resolve-cmake-generator.ps1 -VisualStudioMajor 18
.\scripts\build-dependencies.ps1 -Architecture x86 -VisualStudioMajor 18
cmake -S . -B out\v5.5-build-win32 -G "$generator" -A Win32 -T v145
cmake --build out\v5.5-build-win32 --config Release --parallel
ctest --test-dir out\v5.5-build-win32 -C Release --output-on-failure

.\scripts\build-dependencies.ps1 -Architecture x64 -VisualStudioMajor 18
cmake -S . -B out\v5.5-build-x64 -G "$generator" -A x64 -T v145 `
  -DPDW_DEPENDENCY_ROOT="$PWD\out\dependencies\x64"
cmake --build out\v5.5-build-x64 --config Release --parallel
ctest --test-dir out\v5.5-build-x64 -C Release --output-on-failure

.\scripts\stage-installer-input.ps1 -Architecture Win32 `
  -BuildDirectory out\v5.5-build-win32\Release `
  -Destination out\v5.5-installer-input\Win32
.\scripts\stage-installer-input.ps1 -Architecture x64 `
  -BuildDirectory out\v5.5-build-x64\Release `
  -Destination out\v5.5-installer-input\x64
.\scripts\build-installer.ps1 `
  -Win32ApplicationDirectory out\v5.5-installer-input\Win32 `
  -X64ApplicationDirectory out\v5.5-installer-input\x64 `
  -OutputDirectory out\v5.5-installer
.\scripts\audit-installer.ps1 `
  -Setup 'out\v5.5-installer\PDW-v5.5.2-2026-Release-Setup-package\PDW-v5.5.2-2026-Release-Setup.exe'
.\tests\installer_smoke.ps1 `
  -Setup 'out\v5.5-installer\PDW-v5.5.2-2026-Release-Setup-package\PDW-v5.5.2-2026-Release-Setup.exe' `
  -TestRoot out\v5.5-installer-smoke
```

Public builds require the approved signing command and `-RequireSignature`
during installer audit, plus Defender and every other installer audit and smoke
check.

## Compatibility and privacy boundaries

- Preserve POCSAG, FLEX, ACARS, MOBITEX, and ERMES behavior.
- Keep WinMM, WASAPI fallback, serial slicers, `.rec` playback, legacy local
  audio, direct radio, filters, logs, alerts, and every established output.
- Keep network/data outputs disabled by default and independent from capture
  and decoding.
- Never commit or package pager traffic, operator logs, credentials, queues,
  recordings, databases, endpoint IDs from a real machine, or personal INI
  files.
- Use synthetic, redacted, or licensed data for tests and acceptance records.
- Treat source, local builds, packages, Setup, pushed branch, PR, CI, merge,
  tag, signature, and public release as separate states.

## Publication workflow

1. Resolve and review the complete v5.5.2 diff without disturbing unrelated work.
2. Pass the release audit, dual-architecture, package, installer, security and
   privacy and signing gates required for the release channel.
3. Commit and push the release state to fork `master`; wait for exact-head
   Build/Setup and CodeQL results.
4. Download only the exact-head Setup artifact and verify its published
   checksum before creating immutable tag `v5.5.2`.
5. Publish the signed Setup plus its checksum as a normal GitHub release after
   post-sign and Defender verification. GitHub's
   automatic source-code ZIP/TAR archives are not additional PDW installers.
6. Do not publish until application, Setup and uninstaller signatures,
   timestamps, Defender scan, clean-machine smoke and required physical
   acceptance all pass.
7. Retain historical branches and artifacts as rollback evidence.
