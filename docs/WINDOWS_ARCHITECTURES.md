# PDW Windows architecture support

PDW uses one source tree to produce two native Windows applications.

| Release | Intended use | Native dependencies |
| --- | --- | --- |
| x64 | Primary build for current 64-bit Windows systems | x64 OpenSSL, curl, libssh2 and optional x64 receiver DLLs |
| Win32 | Compatibility build for established 32-bit integrations | x86 OpenSSL, curl, libssh2, bundled x86 RTL-SDR and legacy driver assets |

The decoder, filters, configuration schema and output behaviour are shared.
CI must compile and run the complete automated test suite for both targets.
Maintained releases use Visual Studio 2026, MSVC v145, and the explicit
`windows-2025-vs2026` GitHub Actions image.

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
- The x64 package omits the bundled x86 RTL-SDR DLL and old VxD driver files.
- Win32 remains the supported fallback for historical slicer or receiver driver
  installations until those devices pass physical x64 acceptance testing.
- A successful compile and automated test run does not prove compatibility with
  every physical serial adapter, slicer driver or USB receiver.

## Release gates

1. Build pinned dependencies for x64 and x86 independently with Visual Studio
   2026 and MSVC v145 recorded in each dependency lock marker.
2. Build PDW Release for both targets.
3. Run every CTest test on both targets.
4. Confirm each packaged executable PE machine matches its package label.
5. Smoke-test startup on a supported Windows x64 system.
6. Perform receiver and legacy serial hardware acceptance before declaring
   those physical integrations supported on x64.
