# Changelog

## 0.3.0 — MULTI5

- Added complete Italian, English, French, German and Spanish interfaces.
- Added an in-app language selector whose choice is applied at the next launch.
- Added automatic first-launch language detection and persistent selection.
- Embedded human-editable UTF-8 JSON translation catalogs in the executable.
- Localized live audio states, DSP analysis results, WAV errors, plots,
  simulator, positional sessions and export dialogs.
- Kept translation infrastructure independent of Qt LinguistTools so the
  existing Qt 6.5+ build requirements remain unchanged.

## 0.2.0 — Test without hardware

- Added configurable deterministic watch-signal simulator.
- Added synthetic unlock/impulse/drop-like pulse clusters, noise, rate, beat
  error and optional missed impulses.
- Added standard two-position sessions for the target holder: dial up and
  caseback up.
- Added optional advanced six-position sessions, disabled by default.
- Added positional mean and rate-spread summary.
- Moved DSP analysis to Qt Concurrent so audio and UI remain responsive.
- Added persistence for window geometry, audio device, BPH, lift angle and
  position mode.

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
