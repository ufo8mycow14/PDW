# PDW Windows architecture support

PDW uses one source tree to produce two native Windows applications.

| Release | Intended use | Native dependencies |
| --- | --- | --- |
| x64 | Primary build for current 64-bit Windows systems | x64 OpenSSL, curl, libssh2, app-local VC145 runtime and optional x64 receiver DLLs |
| Win32 | Compatibility build for established 32-bit integrations | x86 OpenSSL, curl, libssh2, app-local VC145 runtime and bundled x86 RTL-SDR |

The decoder, filters, configuration schema and output behaviour are shared.
CI must compile and run the complete automated test suite for both targets.
Maintained releases use Visual Studio 2026, MSVC v145, and the explicit
`windows-2025-vs2026` GitHub Actions image.
The installer enforces build 10586 as the technical API floor. Production use
requires a Windows 10/11 edition and build still receiving Microsoft security
servicing (including applicable ESU or LTSC servicing), or a serviced Windows
Server 2016-or-newer release. Windows 7/8/8.1 are outside the current compiler
and Windows SQLite support boundary.

## Build commands

```powershell
$generator = .\scripts\resolve-cmake-generator.ps1 -VisualStudioMajor 18
.\scripts\build-dependencies.ps1 -Architecture x64 -VisualStudioMajor 18
cmake -S . -B out\build-x64 -G "$generator" -A x64 -T v145
cmake --build out\build-x64 --config Release --parallel
ctest --test-dir out\build-x64 -C Release --output-on-failure
```

```powershell
$generator = .\scripts\resolve-cmake-generator.ps1 -VisualStudioMajor 18
.\scripts\build-dependencies.ps1 -Architecture x86 -VisualStudioMajor 18
cmake -S . -B out\build-win32 -G "$generator" -A Win32 -T v145
cmake --build out\build-win32 --config Release --parallel
ctest --test-dir out\build-win32 -C Release --output-on-failure
```

## Compatibility boundary

- Windows cannot load a native DLL built for the other architecture. PDW checks
  receiver DLL PE headers before loading them.
- RTL-TCP is architecture-neutral because it communicates over TCP.
- The x64 package omits the bundled x86 RTL-SDR DLL.
- Both packages carry the reviewed, matching-bitness VC145 release runtime
  DLLs beside PDW. Setup removes every allowlisted runtime filename before an
  architecture switch, preventing an x64-only DLL from remaining in Win32.
  The UCRT remains an operating-system component and is not packaged.
- Current packages do not ship the old untracked `COMPRT.VXD`, `Comprt2.vxd`
  or `xp_driver.zip` Windows 9x/XP-era slicer-driver artifacts. Their former
  opportunistic inclusion depended on files outside the repository and was
  not reproducible. This does not remove PDW's maintained Win32 COM/RS232,
  serial-slicer, sound-card or receiver paths, and Setup does not delete an
  operator's pre-existing files.
- Win32 remains the supported fallback for historical slicer or receiver driver
  installations until those devices pass physical x64 acceptance testing.
- A successful compile and automated test run does not prove compatibility with
  every physical serial adapter, slicer driver or USB receiver.

## Release gates

1. Build pinned dependencies for x64 and x86 independently with Visual Studio
   2026 and MSVC v145 recorded in each dependency lock marker.
2. Build PDW Release for both targets.
3. Run every CTest test on both targets.
4. Confirm the packaged executable and every allowlisted VC runtime PE machine
   match the package label and the runtime file versions are in the v145 14.5x
   family.
5. Require `PDW_BUILD_COMMIT.txt` to identify the exact current clean `HEAD` in
   both architecture outputs and all staged/package artifacts; require each
   installer-input SHA-256 manifest to bind the exact matching architecture
   file set before and after Setup compilation.
6. Smoke-test startup on a supported Windows x64 system.
7. Perform receiver and legacy serial hardware acceptance before declaring
   those physical integrations supported on x64.
