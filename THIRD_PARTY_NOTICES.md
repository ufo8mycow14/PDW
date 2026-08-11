# Third-party components

PDW's Win32 and x64 builds link the following open-source components statically. The
build script downloads the official source archives, checks their SHA-256
hashes before extraction, and installs a copy of each component's license with
the generated dependency set.

| Component | Version | SHA-256 source archive | License |
| --- | --- | --- | --- |
| OpenSSL | 3.5.7 | `A8C0D28A529CA480F9F36CF5792E2CD21984552A3C8E4AA11A24AA31AEAC98E8` | Apache License 2.0 |
| curl/libcurl | 8.21.0 | `AA1B66A70EACE83DC624508745646C08AE561DE512AB403ADFFB93AC87FC72E6` | curl license |
| libssh2 | 1.11.1 | `9954CB54C4F548198A7CBEBAD248BDC87DD64BD26185708A294B2B50771E3769` | Revised BSD license |

Official sources and license information:

- OpenSSL: <https://github.com/openssl/openssl/releases/tag/openssl-3.5.7>
- curl: <https://github.com/curl/curl/releases/tag/curl-8_21_0>
- libssh2: <https://github.com/libssh2/libssh2/releases/tag/libssh2-1.11.1>

OpenSSL continues to provide TLS for PDW's existing SMTP code. The file
transfer feature uses libcurl with Windows Schannel for FTPS and libssh2 with
Windows CNG for SFTP. The dependency build is defined in
`scripts/build-dependencies.ps1`.

## Optional receiver components

Receiver components are separate optional programs/libraries kept below
`Receivers`; they are not statically linked into the current PDW executable,
and their absence cannot prevent legacy PDW inputs from starting.

| Component | Version | SHA-256 distributed binary/archive | License |
| --- | --- | --- | --- |
| RTL-SDR Blog `rtlsdr.dll` (x86) | 1.4.0 | DLL `E14010C2A0DFDE1C85C13D761484509A600FEC4DE6CD2C026C0878DE11155A02`; release archive `7EF33F1304647F65E5E0FDE43637A73D54F076E91E651A3CECC4F55A17FD9815` | GNU GPL v2 |
| Zadig | 2.9 (libwdi 1.5.1) | `4ECAA95DF3DA3621486A043AEF8B3050B8BAFE7C901402871E816229EF82039B` | GNU GPL v3 |

The unmodified matching source archives and complete licence texts are kept
beside each binary. Official sources:

- RTL-SDR Blog: <https://github.com/rtlsdrblog/rtl-sdr-blog/releases/tag/V1.4.0>
- Zadig/libwdi: <https://github.com/pbatard/libwdi/releases/tag/v1.5.1>

Zadig changes Windows USB drivers and is never launched automatically by PDW.
Users must verify the selected USB device before installing WinUSB.

## Microsoft Visual C++ runtime

PDW remains dynamically linked with the Visual Studio 2026/MSVC v145 release
runtime (`/MD`). Portable and installer distributions place only these reviewed
Microsoft redistributable DLL names beside the matching PDW executable:

- both architectures: `concrt140.dll`, `msvcp140.dll`, `msvcp140_1.dll`,
  `msvcp140_2.dll`, `msvcp140_atomic_wait.dll`,
  `msvcp140_codecvt_ids.dll`, and `vcruntime140.dll`;
- x64 only: `vcruntime140_1.dll` (the reviewed VC145 x86 redistributable set
  does not supply that filename).

CMake's `InstallRequiredSystemLibraries` locates the release files for the
current target architecture. Release staging then requires the exact applicable
allowlist and validates each PE machine, original filename, and VC145 14.5x
file-version family. PDW does not distribute a `VC_redist` installer, the UCRT,
debug runtimes, MFC, OpenMP, `vccorlib`, or other compiler-runtime files. The
supported Windows operating-system floor supplies and services the UCRT.

These DLLs are Microsoft copyrighted components and are not relicensed under
PDW's GNU GPL. Redistribution is governed by the Microsoft Visual Studio 2026
license and its distributable-code terms. Microsoft's current Visual Studio
license terms are available at <https://visualstudio.microsoft.com/license-terms/>.

## Excluded historical kernel-driver artifacts

PDW does not distribute the obsolete `COMPRT.VXD`, `Comprt2.vxd`, or
`xp_driver.zip` Windows 9x/XP-era slicer-driver artifacts. They were untracked files
outside the source tree, with no deterministic clean-clone input or current
redistribution review, and are therefore excluded from both architectures.
This packaging boundary does not remove the maintained Win32 COM/RS232 or
serial-slicer implementation, and the installer preserves operator-created
files already present in an existing PDW folder.

## Optional external notification service

The Apprise feature is an HTTPS client integration and does not link or package
Apprise code in the current PDW executable. The validated external deployment
is Apprise API 1.5.1 (MIT License) with Apprise 1.12.0 (BSD 2-Clause License).
Operators host, secure, license, and update that service independently:

- Apprise API: <https://github.com/caronc/apprise-api/releases/tag/v1.5.1>
- Apprise: <https://github.com/caronc/apprise/releases/tag/v1.12.0>

## Optional external local-audio tools

The named SDR# + VB-Audio Cable profile interoperates with two
operator-installed products. Neither product, its installer, driver, source,
license key, or executable is downloaded, copied, linked, or packaged by PDW.

| Product | Reviewed integration | Distribution and licensing boundary |
| --- | --- | --- |
| SDR# (SDRSharp) | Production revision 1921 | Obtained, configured, updated, licensed, and supported by the operator from Airspy; PDW only captures the operator-selected Windows audio endpoint |
| VB-CABLE | Windows Package 45 (32/64/Arm64) | VB-Audio donationware; the operator installs and licenses it directly, and professional use is subject to the vendor's paid-licensing terms |

Official sources:

- SDR# downloads: <https://airspy.com/download/>
- VB-CABLE downloads: <https://vb-audio.com/Cable/>
- VB-Audio licensing: <https://vb-audio.com/Services/licensing.htm>

The profile contains a blank machine endpoint ID and does not configure SDR#
or install a virtual driver. A friendly-name match establishes the intended
endpoint on the operator's machine; PDW then stores and uses its opaque Windows
identity. If that endpoint is unavailable, capture fails closed rather than
silently selecting another recording device.
