# Live input acceptance

This document separates repeatable device evidence from unit tests. Hardware
smoke targets are deliberately excluded from the normal build and CTest suite
because CI runners and developer computers do not have identical receivers or
audio endpoints.

Neither smoke target stores audio samples, decoded messages, credentials, or
device recordings. They open the current Windows default capture endpoint for
approximately one second, report content-free counts, release it, and exit.

## Repeatable commands

From a configured Win32 build tree:

```powershell
cmake --build out\build-win32 --config Release --target PDWWinmmDeviceSmoke
.\out\build-win32\Release\PDWWinmmDeviceSmoke.exe

cmake --build out\build-win32 --config Release --target PDWWasapiDeviceSmoke
.\out\build-win32\Release\PDWWasapiDeviceSmoke.exe
```

`PDWWinmmDeviceSmoke` requests the exact legacy PDW format: unsigned 8-bit,
mono, 44.1 kHz PCM through `WAVE_MAPPER`. `PDWWasapiDeviceSmoke` opens the
Windows default capture endpoint through the same event-driven source used by
PDW's fallback path.

## 10 August 2026 development-machine snapshot

| Gate | Result | Evidence |
| --- | --- | --- |
| Manual application startup | Pass | Main window titled `PDW v4.5.0 Beta` was responsive; measured from 243 ms to 1,189 ms, including 422 ms after the final clean build. |
| Windows `/startup` delay | Pass | Main window was responsive from 5,141 ms to 6,906 ms after the intended five-second settle delay. |
| Legacy WinMM capture | Pass | One capture device; 44,100 bytes at 44.1 kHz, 8-bit mono. |
| WASAPI fallback capture | Pass | 48,000 samples at 48 kHz on the final rerun; one initial discontinuity notification. |
| Automated Win32 suite | Pass | All 29 non-hardware CTest tests passed. |

These results establish that both Windows audio APIs can open and capture on
this machine. They do not establish hot-plug recovery, device-loss recovery,
every Windows audio device, or physical radio compatibility.

## Remaining live-input matrix

- Remove or disable the active audio endpoint during capture, restore it, and
  verify PDW returns safely to a known input.
- Repeat WinMM and WASAPI smoke checks with each intended physical or virtual
  discriminator-audio device.
- Exercise a real `rtl_tcp` server through connect, tune change, disconnect,
  reconnect, and malformed-header cases. The isolated demodulator tests cover
  protocol parsing without claiming live-server acceptance.
- Test each supported RTL-SDR USB package and device index. Confirm a missing
  optional DLL or absent receiver never prevents normal PDW startup.
- Replay a synthetic, redacted, or redistribution-licensed recording after
  each failure and confirm the previous live source is restored.

Record device model, driver version, Windows version, selected input, expected
recovery, actual recovery, and whether any operator action was required. Never
attach private pager traffic, raw captures, logs, queue files, or credentials
to a public acceptance report.
