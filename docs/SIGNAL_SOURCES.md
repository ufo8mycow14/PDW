# Radio, recording, and replay

Open **Settings > Decoder and input > Radio and replay**. These inputs are
additional choices inside the same `PDW.exe`; they do not create a separate
edition or background helper.

## Local audio and serial compatibility

The existing Input Setup dialog remains the authority for legacy serial and
local audio configuration. Local audio tries the original mono unsigned 8-bit
WinMM path first. If Windows rejects that format, PDW starts event-driven
WASAPI capture, converts supported PCM/float formats to normalized mono, and
feeds the existing protocol routines. Device loss triggers a bounded retry.

Serial two-level and four-level slicers, inversion settings, sample-rate/audio
configuration presets, `.rec` playback, and WAV alerts are unchanged.

## RTL-TCP

Select **RTL-TCP network radio**, enter host, port, frequency, IQ rate, audio
rate, gain/AGC, and PPM correction, then use **Test selected source**. PDW
validates the `RTL0` header, configures the tuner, FM-demodulates and resamples
IQ in process, and reconnects after a dropped connection. No decoded text is
sent to the RTL-TCP server.

## RTL-SDR USB

Select **RTL-SDR USB** when a compatible 32-bit `rtlsdr.dll` or
`librtlsdr.dll` is installed beside PDW or on the DLL search path. PDW loads it
dynamically, so missing or faulty radio support cannot prevent legacy PDW from
starting. The device test opens the selected index and applies tuner settings
without changing the saved source until **OK** is selected.

## Diagnostic WAV and SigMF

Recording observes normalized samples without changing the decoder input. WAV
is written as 16-bit mono. SigMF is written as `rf32_le` plus sample-rate
metadata. The in-memory diagnostic safety limit is 25 million samples; PDW
reports when that limit truncates a recording.

Replay accepts mono/stereo PCM8, PCM16, or float32 WAV and real float32 SigMF.
It uses the recording's sample rate, resets protocol timing, runs through the
normal PDW decoder functions in real time, then restores the exact prior live
audio/radio/serial source. Stop a diagnostic recording before starting replay.

Only use synthetic, redacted, or explicitly licensed recordings in bug
reports or public repositories. Pager traffic can contain private information.
