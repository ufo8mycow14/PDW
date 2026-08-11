# Repository file audit

Audited: 11 August 2026

This audit defines the files intentionally maintained for PDW v5 2026 Release and later.
PDW is one native C++ source tree with two CMake build targets: x64 and Win32.
CMake is the only maintained project definition.

## Visual Basic and Visual Studio finding

The repository contains no Visual Basic projects or source files. No `.vb`,
`.vbp`, `.bas`, `.frm`, or `.cls` file is tracked. The older files that looked
similar were Visual C++ 6 and Visual Studio 2017 project metadata.

Those project files were removed because they were not used by CMake, GitHub
Actions, tests, or release packaging, and they no longer described the current
x64/Win32 source, pinned dependencies, test targets, or executable name.

## Removed files

### Obsolete Visual C++ and Visual Studio definitions

- `PDW.dsp`, `PDW.dsw`, and `PDW.mak` - Visual C++ 6 project/workspace/NMAKE
  output.
- `PDW.dep`, `PDW.opt`, and `PDW.plg` - generated VC6 dependency, user-state,
  and build-log files.
- `pdw_vs2017.sln`, `pdw_vs2017.vcxproj`,
  `pdw_vs2017.vcxproj.filters`, and `pdw_vs2017.vcxproj.user` - superseded
  Visual Studio 2017 project state.
- `Rsrc.aps` and `Rsrc.clw` - generated resource-editor/ClassWizard caches.

### Redundant historical and unused files

- `pdw3.1-full.zip` - a duplicate historical runtime archive. Git history and
  upstream releases preserve it; current packaging never consumed it.
- `Headers/html.h` - declarations for a removed 2003 HTML logger with no
  implementation or active include. Current publishing uses tested
  `utils/publishing_core.*` code.
- `utils/globals.h` - an unreferenced legacy serial/debug header.
- `utils/OSTYPE.C` - an unused older duplicate of the maintained
  `utils/Ostype.cpp` implementation.
- `GFX/close.bmp` and `GFX/pdwlogo3.bmp` - unreferenced bitmap assets not
  embedded by `Rsrc.rc`.
- `resrc1.h` - VC6 resource-editor defaults, now maintained at the end of
  `Headers/Resource.h`.
- `COMPRT.VXD`, `Comprt2.vxd`, and `xp_driver.zip` - obsolete, untracked
  Windows 9x/XP-era slicer-driver artifacts that an older packaging script copied
  opportunistically from outside the repository. They are no longer release
  inputs. PDW's maintained Win32 COM/RS232 and serial-slicer code remains, and
  Setup preserves pre-existing operator files.

## Intentionally retained files

- `pdw-manual.pdf` is copied beside developer builds and into portable
  packages for F1 help.
- `Receivers/RTL-SDR/rtlsdr.dll` is the documented x86 receiver used by the
  Win32 package. The x64 package explicitly removes it.
- `Receivers/RTL-SDR/rtl-sdr-blog-V1.4.0-source.zip` and its GPL notice retain
  the corresponding receiver source and redistribution evidence.
- `Receivers/Driver Tools/zadig-2.9.exe`, the libwdi source ZIP, and GPL notice
  are the documented optional driver-installation set.
- `packaging/PDW.INI` is the sanitized release default, not operator data.
  Filter persistence is owned by the Capcode Directory in `pdw-history.sqlite3`;
  fresh packages intentionally do not ship `filters.ini`.
- `packaging/PDW-Adelaide-FLEX.INI` is the sanitized, explicit clean-install
  SDR# + VB-Audio Cable profile. It contains no machine endpoint ID, traffic,
  credential, or filter data and does not replace `packaging/PDW.INI`.
- `docs/SDRSHARP_VBCABLE_PROFILE.md` documents the external/operator-installed
  signal path, stable-identity fail-closed boundary, endpoint-specific WASAPI
  capture, and lawful-use acceptance.
- `packaging/Wavfiles` and `packaging/Legacy` contain reviewed, non-private
  installer/package inputs required for reproducible clean-clone staging.
- `installer/PDW.iss` is the maintained guided Windows Setup definition; Inno
  Setup project output is generated and is not tracked.
- Every retained C/C++ translation unit is referenced by a CMake application,
  test, or hardware-smoke target. Every retained GFX bitmap/icon is embedded by
  `Rsrc.rc` and listed in `GFX/CMakeLists.txt`.

## Architecture boundary

| File group | Win32 | x64 | Reason |
| --- | --- | --- | --- |
| Application source, resources, tests, docs | Included | Included | Shared behavior and UI |
| Pinned OpenSSL/curl/libssh2 dependencies | x86 build | x64 build | Native code must match the process |
| Bundled `rtlsdr.dll` | Included | Excluded | Distributed DLL is x86 |
| Obsolete external Windows 9x/XP-era slicer-driver artifacts | Excluded | Excluded | Untracked ambient files are not deterministic release inputs; maintained serial code remains |
| RTL-TCP support | Included | Included | TCP boundary is architecture-neutral |

## Required validation after file cleanup

1. Configure clean CMake projects for Win32 and x64.
2. Build the application and every configured Release target for both.
3. Pass all CTest tests for both architectures.
4. Verify PE machine type, file/product version, and embedded manifest.
5. Run native startup and Settings visual smoke for both architectures.
6. Generate and independently audit both portable packages.
7. Build the guided installer, run isolated x64 and Win32
   install/co-location/upgrade/uninstall smoke, scan it, and verify its signature
   before public release.
8. Confirm the Git tree and every attached worktree are clean before
   publication.

## Predecessor validation evidence

The results below belong to the merged v5.4 baseline and earlier releases.
They remain useful regression evidence but do not prove the v5.5 candidate:

- Fresh Visual Studio 2026 Build Tools 18.8.2, MSVC 19.51.36252/v145, and
  CMake Release builds completed from new build directories for Win32 and x64.
- Win32 and x64 each pass 29 of 29 CTest tests.
- Optional WinMM and WASAPI hardware-smoke targets compile for both
  architectures.
- PE machine values are Win32 `0x014C` and x64 `0x8664`; both executables
  report file version `5.4.0.0` and product version `5.4.0 2026 Release`.
- Both native executables passed a five-second hidden startup smoke on
  11 August 2026; receiver-specific live-hardware acceptance remains open.
- Both embedded manifests report `5.4.0.0`, architecture-neutral assembly
  metadata, and Per-Monitor V2 DPI awareness.
- Native UI review remains predecessor-v5 evidence. The v5.4 resources and
  executable metadata use the shared **PDW v5.4 2026 Release** identity, while
  current Light/Dark, compact-size, new-dialog, About, and Settings visual
  confirmation remains open.
- The preceding v4.6.0 repository-audit packages contain 318 Win32 and 310 x64
  manifest entries. Independent verification found zero missing,
  changed, unlisted, private-runtime, non-empty-secret, or obsolete-source
  files. The packaged audit document matches the committed Git blob.
- The v4.6.1 candidate packages contain 327 Win32 and 319 x64 manifest entries;
  the additional maintained policy and audit files account for the increase.
  Independent validation again found zero hash mismatch, missing, unlisted,
  private-runtime, non-empty-secret, or obsolete-source files.
- Win32 retains the intentional x86 RTL-SDR DLL and four mirrored legacy VxD
  copies; x64 contains neither.
- The v5.4 guided installer passes isolated x64 and Win32 installation,
  co-located settings, upgrade-preservation, exact renamed-v5/v5.1/v5.2/v5.3 executable
  cleanup, and uninstall-preservation smoke. Microsoft Defender reports no
  threat in the unsigned local candidate.
- `scripts/audit-release.ps1` derives and enforces the current identity, both CI architectures,
  portable-package generation, installer build/smoke coverage, recursive
  fresh-`filters.ini` exclusion, architecture-marker and receiver-backup
  coverage, Visual Basic review, obsolete/ambient-file exclusion,
  dependency-notice alignment, and prepared-statement SQL safeguards.
- Operator-selected message archives fail closed unless current Windows SQLite
  connection defenses and a first-on-open bounded quick integrity check pass.
- Trusted Authenticode signing remains mandatory before the installer is
  promoted as the public stable release.

## PDW v5.5 validation state

The v5.5 source identity is **PDW v5.5 2026 Release**, product version
`5.5.0 2026 Release`, file/manifest version `5.5.0.0`, and branch
`pdw-v5.5-sdr-vbcable-reconciliation`. The candidate adds an explicit
clean-install Adelaide FLEX profile plus stable endpoint identity and
endpoint-specific WASAPI capture.
The maintained v5.5 release build uses Visual Studio 2026/MSVC v145 on the
explicit `windows-2025-vs2026` runner for both x64 and Win32. Dependency locks
bind the exact compiler, CMake generator/version, toolset and resolver recipe;
Visual Studio 2022 remains a local rollback path only.
Release builds copy only the explicit architecture-matched Microsoft VC145
runtime allowlist beside PDW; package and installer staging PE/version-validate
that set, reject the UCRT and installers, and bind each output to the exact
clean source commit recorded in `PDW_BUILD_COMMIT.txt` at link time. Tracked
release inputs come from one immutable archive of that commit, mutable build
files are hash-checked across copying, and each architecture's exact staged file
set is bound by `PDW_INSTALLER_INPUT_SHA256SUMS.txt` through Setup compilation.
SDR# and VB-CABLE are external/operator-installed and are not repository or
package dependencies. The profile does not create, import, replace, or modify
Capcode Directory entries or `filters.ini`.

No fresh v5.5 validation result is recorded yet. Release audit, clean Win32 and
x64 builds/tests, optional device-smoke compilation, PE/version/manifest/About
inspection, stable-identity/endpoint-specific-WASAPI and explicit-apply tests, native UI matrices, independent
portable-package audit, guided installer/profile/upgrade/uninstall smoke,
Defender, exact-head CI/CodeQL, Authenticode signing, and clean-worktree review
all remain pending. Evidence must name the exact candidate commit and artifact;
predecessor results must not be relabelled as v5.5 results.
