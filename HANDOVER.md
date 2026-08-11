# PDW v5.5 2026 Release handover

Updated: 11 August 2026

## Current release identity

- Repository: `C:\PDW Update\tmp\pdw-v5.5-sdr-vbcable`
- Active branch: `pdw-v5.5-sdr-vbcable-reconciliation`
- Baseline: fork `master` at `651cedfd2d53a03c27f673d899d9774e0d91f76b`
- Product/display name: **PDW v5.5 2026 Release**
- Executable: `PDW v5.5 2026 Release.exe`
- Product version: `5.5.0 2026 Release`
- File/manifest version: `5.5.0.0`
- Installer: `PDW-v5.5-2026-Release-Setup.exe`
- Portable packages: `PDW-v5.5-2026-Release-Win32` and
  `PDW-v5.5-2026-Release-x64`

PDW remains one native C++ product with mandatory Win32 and x64 targets. The
guided installer and portable packages are two delivery forms of that same
application; they do not define separate editions or decoder behavior.
The maintained release toolchain is Visual Studio 2026/MSVC v145 on the
`windows-2025-vs2026` hosted image. Visual Studio 2022/v143 remains an explicit
local rollback option, not the v5.5 release target. Dependency locks record the
exact compiler, generator, toolset, CMake version and resolver recipe.

`Headers/version.h` is the authoritative current identity. CMake reads the
display name for the executable output, while resources, the manifest,
workflow artifact names, packaging, Setup, the main title, About, and current
documentation must agree with it before release.

## v5.5 scope

The release adds an explicit clean-install **SDR# + VB-Audio Cable (Adelaide
FLEX)** profile beside the standard PDW settings. Its compatibility boundary
is narrow:

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

The profile does not enable a network, notification, publishing, database, or
other data-output destination.

## Installer and package boundary

The maintained Setup definition is `installer/PDW.iss`. The v5.5 candidate is
intended to:

- offer x64 or Win32 on 64-bit Windows and use Win32 on 32-bit Windows;
- keep the application and mutable operator data in one selected PDW folder;
- preserve existing settings, Capcode Directory data, legacy `filters.ini`
  recovery files, receivers, sounds, logs, queues, recordings, and databases;
- offer the named profile only during a genuinely clean installation;
- never install a new `filters.ini` or overwrite an existing `PDW.INI`;
- remove only exact renamed predecessor executables, including v5.4; and
- support trusted signing of the application, Setup, and uninstaller.

Win32 may retain reviewed x86-only receiver assets. x64 must exclude those
binaries and use architecture-matched libraries or the architecture-neutral
`rtl_tcp` path. Portable packaging remains supported and must carry the same
clean-install profiles and documentation without private runtime data.

## Dependency review

The 11 August 2026 review retains the pinned OpenSSL 3.5.7 LTS, curl 8.21.0,
libssh2 1.11.1, Windows `winsqlite3`, and operator-managed MySQL ODBC boundary.
Oracle's July 2026 CPU and the current Connector/ODBC 26.7.0 download were
reviewed. Inno Setup 7.0.2 is available, but v5.5 deliberately retains pinned
6.7.3 because changing installer-compiler major version during the profile and
upgrade change would broaden release risk; migration to 7 remains a separate
dual-architecture installer project.

The reviewed external-profile references are SDR# production revision 1921 and
VB-CABLE Package 45. Neither is packaged. Operators obtain, secure, support,
and license them independently; VB-CABLE professional use is subject to the
vendor's licensing terms.

## Current verification state

The fork `master` baseline at `651cedf` has successful x64, Win32, guided Setup,
and CodeQL checks. Those are predecessor-v5.4 baseline results, not evidence for
the uncommitted v5.5 candidate.

At this handover, all v5.5 release gates remain pending unless a later dated
record identifies the exact tested commit and artifacts:

- release audit and complete diff review;
- fresh Win32 and x64 Release configure/build/CTest runs;
- optional WinMM and WASAPI device-smoke compilation for both architectures;
- stable-identity and endpoint-specific WASAPI profile tests;
- PE machine, file/product version, manifest, main-title and About verification;
- native Light/Dark, compact-size, keyboard, High Contrast, and DPI acceptance;
- independent Win32 and x64 portable-package audit;
- guided Setup clean-profile, standard-profile, upgrade, co-location,
  predecessor-cleanup, uninstall, and preservation smoke;
- Microsoft Defender scanning;
- PR CI and CodeQL on the exact candidate head; and
- trusted Authenticode signing and timestamp verification for public artifacts.

No v5.5 portable package, installer, signature, tag, GitHub Release, or public
deployment should be described as complete until its corresponding gate has
actually passed.

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
  -Setup 'out\v5.5-installer\PDW-v5.5-2026-Release-Setup-package\PDW-v5.5-2026-Release-Setup.exe'
.\tests\installer_smoke.ps1 `
  -Setup 'out\v5.5-installer\PDW-v5.5-2026-Release-Setup-package\PDW-v5.5-2026-Release-Setup.exe' `
  -TestRoot out\v5.5-installer-smoke
```

Public builds additionally require the approved signing command and
`-RequireSignature` during installer audit.

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

1. Resolve and review the complete v5.5 diff without disturbing unrelated work.
2. Pass the local dual-architecture, package, installer, UI, security, and
   privacy gates above.
3. Commit and push `pdw-v5.5-sdr-vbcable-reconciliation` to the writable fork.
4. Open a draft PR against fork `master` and wait for exact-head Build/Setup
   and CodeQL results.
5. Merge only after required source gates pass.
6. Do not publish a stable installer until application, Setup, and uninstaller
   signatures, timestamps, Defender scan, and clean-machine smoke all pass.
7. Retain historical branches and artifacts as rollback evidence.
