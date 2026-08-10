# Repository file audit

Audited: 10 August 2026

This audit defines the files intentionally maintained for PDW v4.6.1 and later.
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

## Intentionally retained files

- `pdw-manual.pdf` is copied beside developer builds and into portable
  packages for F1 help.
- `Receivers/RTL-SDR/rtlsdr.dll` is the documented x86 receiver used by the
  Win32 package. The x64 package explicitly removes it.
- `Receivers/RTL-SDR/rtl-sdr-blog-V1.4.0-source.zip` and its GPL notice retain
  the corresponding receiver source and redistribution evidence.
- `Receivers/Driver Tools/zadig-2.9.exe`, the libwdi source ZIP, and GPL notice
  are the documented optional driver-installation set.
- `packaging/PDW.INI` and `packaging/filters.ini` are sanitized release
  defaults, not operator data.
- Every retained C/C++ translation unit is referenced by a CMake application,
  test, or hardware-smoke target. Every retained GFX bitmap/icon is embedded by
  `Rsrc.rc` and listed in `GFX/CMakeLists.txt`.

## Architecture boundary

| File group | Win32 | x64 | Reason |
| --- | --- | --- | --- |
| Application source, resources, tests, docs | Included | Included | Shared behavior and UI |
| Pinned OpenSSL/curl/libssh2 dependencies | x86 build | x64 build | Native code must match the process |
| Bundled `rtlsdr.dll` | Included | Excluded | Distributed DLL is x86 |
| Legacy VxD/serial support assets | Included when present | Excluded | Compatibility-only Win32 files |
| RTL-TCP support | Included | Included | TCP boundary is architecture-neutral |

## Required validation after file cleanup

1. Configure clean CMake projects for Win32 and x64.
2. Build the application and every configured Release target for both.
3. Pass all CTest tests for both architectures.
4. Verify PE machine type, file/product version, and embedded manifest.
5. Run native startup and Settings visual smoke for both architectures.
6. Generate and independently audit both portable packages.
7. Confirm the Git tree and every attached worktree are clean before
   publication.

## Current validation state

- Fresh Visual Studio 2022/CMake Release builds completed from new build
  directories for Win32 and x64.
- Win32 and x64 each pass 24 of 24 CTest tests.
- Optional WinMM and WASAPI hardware-smoke targets compile for both
  architectures.
- PE machine values are Win32 `0x014C` and x64 `0x8664`; both executables
  report file version `4.6.1.0` and product version `4.6.1 Beta`.
- Both embedded manifests report `4.6.1.0`, architecture-neutral assembly
  metadata, and Per-Monitor V2 DPI awareness.
- Native UI smoke passes for both builds with 10 Settings destinations, an
  unclipped 769x440 dark Backup / Restore dialog, and correct modal recovery.
- The preceding v4.6.0 repository-audit packages contain 318 Win32 and 310 x64
  manifest entries. Independent verification found zero missing,
  changed, unlisted, private-runtime, non-empty-secret, or obsolete-source
  files. The packaged audit document matches the committed Git blob.
- Win32 retains the intentional x86 RTL-SDR DLL and four mirrored legacy VxD
  copies; x64 contains neither.
- `scripts/audit-release.ps1` passes for v4.6.1 and enforces release identity,
  both CI architectures, Visual Basic review, obsolete-file exclusion,
  dependency-notice alignment, and prepared-statement SQL safeguards.
