# Changelog

## 0.1.0 — Engineering preview

- Added Qt 6 desktop interface for Windows, macOS and Linux.
- Added audio-input discovery and hot-plug refresh.
- Added raw mono acquisition from UInt8, Int16, Int32 and Float32 devices.
- Added USB sensor level and clipping indication.
- Added PCM/Float WAV import and PCM16 WAV export.
- Added automatic detection from 7,200 to 43,200 A/h.
- Added manual BPH selection.
- Added rate, beat error, SNR, jitter and confidence calculations.
- Added time-strip and waveform rendering without deprecated Qt Charts.
- Added CSV measurement export.
- Added tests for 7,200, 9,000, 14,400, 18,000, 19,800, 21,600 and
  28,800 A/h, positive/negative rates, beat error, missed impulses and silence.
- Added multi-platform CI configuration.
- Deliberately withheld amplitude until real-signal validation is complete.
