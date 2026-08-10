# PDW receiver packages

PDW keeps optional native receiver files in this folder so they do not replace
or interfere with the legacy sound-card and serial inputs.

- `RTL-SDR` is the bundled 32-bit standard package for RTL2832U-compatible
  devices, including RTL-SDR Blog V3, V4 and V4L receivers.
- `Driver Tools` contains the optional Zadig utility used to install WinUSB for
  an RTL-SDR device. PDW never runs it or changes a Windows driver automatically.
- `Custom` is created when a user chooses **Add receiver...**. PDW validates the
  selected 32-bit librtlsdr-compatible DLL and copies it with neighbouring DLL
  dependencies into a self-contained package.

Only import receiver DLLs obtained from a trusted vendor or project. A native
DLL runs code inside PDW when it is validated and used; API compatibility does
not prove that an unknown binary is safe.

An arbitrary vendor DLL cannot be assumed to use the librtlsdr API. Other
receivers remain usable through PDW's legacy audio input or an RTL-TCP-compatible
network bridge without changing PDW's decoder.
