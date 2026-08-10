# Radio, recording, and replay

Open **Settings > Decoder and input > Radio and replay**. These inputs are
additional choices inside the same `PDW v5.1 2026 Release.exe`; they do not create a separate
edition or background helper.

## Local audio and serial compatibility

The existing Input Setup dialog remains the authority for legacy serial and
local audio configuration. Local audio tries the original mono unsigned 8-bit
WinMM path first. If Windows rejects that format, PDW starts event-driven
WASAPI capture, converts supported PCM/float formats to normalized mono, and
feeds the existing protocol routines. Device loss triggers a bounded retry.

Serial two-level and four-level slicers, inversion settings, sample-rate/audio
configuration presets, `.rec` playback, and WAV alerts are unchanged.

Optional hardware smoke executables verify the current default WinMM and
WASAPI capture endpoints without saving any samples. They are excluded from
the normal CI suite because audio devices vary between computers. Build and
run instructions plus the maintained device matrix are in
`docs/LIVE_INPUT_ACCEPTANCE.md`.

## RTL-TCP

Select **RTL-TCP compatible network receiver**, enter host, port, frequency,
IQ rate, audio rate, 5-25 kHz NFM bandwidth, gain/AGC, and PPM correction,
then use **Test selected source**. PDW
validates the `RTL0` header, configures the tuner, FM-demodulates and resamples
IQ in process, and reconnects after a dropped connection. No decoded text is
sent to the RTL-TCP server.

**Enhanced IQ filtering and resampling** is an optional RTL-only signal
conditioner. It applies a 60 dB windowed-sinc polyphase channel filter before
the FM discriminator, then applies a second anti-alias filter while converting
the discriminator stream to the configured audio rate. This can reduce
out-of-channel energy and resampling aliases without changing POCSAG or FLEX
protocol logic. It is disabled by default; with the box clear, PDW executes
the original one-pole low-pass and averaging path sample-for-sample.

The NFM value retains its established interpretation as the one-sided filter
cutoff. Filtering cannot restore samples lost to tuner overload, clipping,
USB loss, or insufficient RF signal. Compare the same legal diagnostic
recording with the option off and on before retaining it for a receiver.

## RTL-SDR USB

Select **Direct RTL-SDR USB receiver**. The Win32 distribution includes a
standard 32-bit receiver package for common RTL2832U devices and RTL-SDR Blog
V3, V4 and V4L models under `Receivers\RTL-SDR`; the x64 package requires a
trusted matching x64 library or RTL-TCP. PDW lists connected devices by name
and index. The device test opens the selected receiver and applies tuner
settings without changing the saved source until **OK** is selected.

Use **Add receiver...** to import a matching-architecture `rtlsdr.dll` or
`librtlsdr.dll`. PDW checks the PE architecture and required receive API, then
copies the primary library and neighbouring DLL dependencies into a portable
`Receivers\Custom` package. Existing compatible DLLs beside
`PDW v5.1 2026 Release.exe` remain selectable as a legacy fallback. Arbitrary vendor
APIs are not loaded as if they were librtlsdr; those receivers can feed PDW
through local/virtual audio or an RTL-TCP-compatible bridge.

The optional Zadig utility and matching source are under `Receivers\Driver
Tools`. It can install WinUSB for RTL-SDR hardware, but it requires
administrator approval and selecting the exact receiver interface. PDW never
launches it or changes a Windows driver automatically.

## Diagnostic WAV and SigMF

Recording observes normalized samples without changing the decoder input. WAV
is written as 16-bit mono. SigMF is written as `rf32_le` plus sample-rate
metadata. The in-memory diagnostic safety limit is 25 million samples; PDW
reports when that limit truncates a recording.

Replay accepts mono/stereo PCM8, PCM16, or float32 WAV and real float32 SigMF.
It uses the recording's sample rate, resets protocol timing, runs through the
normal PDW decoder functions in real time, then restores the exact prior live
audio/radio/serial source. Stop a diagnostic recording before starting replay.

The same dialog shows a rolling discriminator waveform, audio spectrum,
waterfall, receive-quality history, level, noise, clipping, eye opening, signal score, corrected versus
uncorrectable errors, and FLEX phase A-D error totals. **Calibrate replay...**
tests all 1,000 custom threshold/centering/resync combinations against the
selected recording and offers the best signal-based result. Applying it uses
PDW's existing Custom configuration; every legacy receiver preset remains
available. This is an operator-approved suggestion, not a replacement for
protocol-level frame/error comparison on representative recordings.

Only use synthetic, redacted, or explicitly licensed recordings in bug
reports or public repositories. Pager traffic can contain private information.
