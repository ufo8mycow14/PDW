# RTL-SDR standard receiver pack

This package contains the 32-bit `rtlsdr.dll` from RTL-SDR Blog Windows Release
V1.4.0. It supports common RTL2832U receivers and the RTL-SDR Blog V3, V4 and
V4L models when used with the Win32 PDW build. The x64 PDW package omits this
DLL and requires a trusted matching x64 librtlsdr-compatible library or RTL-TCP.

The receiver must use a libusb-compatible Windows driver. If Windows has not
already configured it, use the optional Zadig utility in `../Driver Tools` and
select WinUSB for the RTL-SDR device only. Replacing the driver for the wrong USB
device can stop that device working, so PDW never performs this step itself.

Provenance:

- Project: https://github.com/rtlsdrblog/rtl-sdr-blog
- Release: V1.4.0
- Release archive SHA-256: `7EF33F1304647F65E5E0FDE43637A73D54F076E91E651A3CECC4F55A17FD9815`
- Bundled x86 `rtlsdr.dll` SHA-256: `E14010C2A0DFDE1C85C13D761484509A600FEC4DE6CD2C026C0878DE11155A02`
- Licence: GNU GPL version 2

The matching source archive and licence are included in this folder.
