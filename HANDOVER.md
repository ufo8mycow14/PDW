# PDW v5.5.2 2026 Release handover

Updated: 12 August 2026

## Current release identity

- Repository: `C:\PDW Update\PDW-source`
- Active branch: fork `master`
- Release tag: `v5.5.2` (immutable once published)
- Product/display name: **PDW v5.5.2 2026 Release**
- Executable: `PDW v5.5.2 2026 Release.exe`
- Product version: `5.5.2 2026 Release`
- File/manifest version: `5.5.2.0`
- Installer: `PDW-v5.5.2-2026-Release-Setup.exe`
- Portable packages: `PDW-v5.5.2-2026-Release-Win32` and
  `PDW-v5.5.2-2026-Release-x64`

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

## v5.5.2 scope

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

## Dependency review

The 12 August 2026 review retains the pinned OpenSSL 3.5.7 LTS, curl 8.21.0,
libssh2 1.11.1, Windows `winsqlite3`, and operator-managed MySQL ODBC boundary.
Oracle's July 2026 CPU and the released Connector/ODBC 9.7 line were reviewed;
26.7.0 is documented but not yet released. Inno Setup 7.0.2 is available, but
v5.5.2 deliberately retains pinned
6.7.3 because changing installer-compiler major version during the profile and
upgrade change would broaden release risk; migration to 7 remains a separate
dual-architecture installer project.

The reviewed external-profile references are SDR# production revision 1921 and
VB-CABLE Package 45. Neither is packaged. Operators obtain, secure, support,
and license them independently; VB-CABLE professional use is subject to the
vendor's licensing terms.

## Current verification state

The v5.5.2 automated release gate requires clean Visual Studio 2026/MSVC v145
x64 and Win32 builds, 35 CTest tests per architecture, both optional
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
