# Changelog

## 0.3.3 — Quiet Confidence and Amplitude

- Calculates diagnostic SNR in the escapement pulse band instead of allowing
  low-frequency room noise to dominate the quality indicator.
- Median-filters SNR and jitter over five overlapping analysis windows.
- Adds exponential confidence smoothing so the percentage changes gradually.
- Reduces broadband SNR weight and gives periodic lock strength more weight.
- Detects robust three-phase tick and tock profiles independently.
- Enables amplitude when both profiles produce physically plausible,
  mutually consistent values for the configured lift angle.
- Adds configurable known amplitude to the deterministic simulator.
- Adds regression tests for amplitude and confidence-display stability.

## 0.3.2 — Temporal Guard

- Fixed false 0.00 ms beat-error readings caused by symmetric full-cycle
  correlation cancelling the tick/tock displacement.
- Changed beat-error analysis to directional tick-to-tock correlation.
- Added a five-window median stabilizer for displayed rate and beat error.
- Rejects isolated large rate jumps and temporary BPH harmonic changes.
- Accepts a genuinely changed measurement after three coherent confirmations.
- Resets temporal history when starting a session, opening a WAV, running the
  simulator or changing the selected BPH.
- Added regression tests for isolated -500 s/day spikes and confirmed changes.
- Revalidated the 30-second USB recording across thirteen overlapping windows.

## 0.3.1 — Real Signal Lock

- Replaced peak-to-peak rate calculation with a long-baseline FFT
  autocorrelation lock.
- Added robust cycle-period estimation with median absolute deviation checks.
- Added constant-fraction transient timing so changing acoustic resonances no
  longer move the detected beat.
- Added phase locking and duplicate rejection for the time-strip trace.
- Reworked beat-error estimation around a robust folded cycle profile.
- Rejects unlocked windows instead of publishing physically meaningless rate
  spikes.
- Validated against the first 30-second recording from the target 48 kHz USB
  contact microphone and a separate same-watch TG reference measurement.
- Added a deterministic stress test with changing impulse shapes, higher
  noise, and missed beats.

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
