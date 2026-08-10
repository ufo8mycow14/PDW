# Decoder regression fixtures

This directory is reserved for redistributable decoder evidence. A fixture may
be synthetic, redacted, or accompanied by an explicit redistribution licence.
Do not commit recordings of private pager traffic.

Each fixture set will contain:

- the source WAV, SigMF recording, legacy `.rec` file, or raw symbol stream;
- a small metadata file describing sample rate, modulation, baud/symbol rate,
  polarity, signal impairments, and the generator or licence;
- expected legacy output, enhanced output, corrected-bit counts, and candidate
  confidence;
- expected filter matches and published redaction output where applicable.

Minimum coverage before enabling enhanced live decoding:

- POCSAG 512, 1200, and 2400 with numeric, alpha, and tone-only messages;
- FLEX 1600 two-level, 3200 two-level, and 3200/6400 four-level with phases
  A-D represented;
- normal and inverted polarity, DC offset, low amplitude, clipping, timing
  drift, adjacent noise, and buffer-boundary splits;
- serial two-level, serial four-level, WinMM audio, and recording playback;
- filtering, duplicate consolidation, and valid conflict retention.

The first synthetic coverage lives in `audio_signal_core_tests.cpp`,
`signal_recording_core_tests.cpp`, and `rtl_tcp_source_tests.cpp`. It tests
normalization, exhaustive legacy PCM8 equivalence, DC-offset tolerance,
two-level polarity, four-level symbol mapping, preservation of the legacy
FLEX A/C decision, confidence, reset behaviour, POCSAG-like and FLEX-like
signal generation, WAV/SigMF round trips, and FM discriminator/resampling
without using captured traffic.
