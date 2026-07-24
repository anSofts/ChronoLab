# Analyzer design

ChronoLab 0.3.1 uses a deterministic signal-processing pipeline:

1. generate an RMS envelope and adaptive transient candidates for BPH scoring;
2. isolate the sharp escapement content with independent second-order filters;
3. rectify and downsample the pulse signal to a safe analysis rate;
4. calculate long-baseline autocorrelation with an internal radix-2 FFT;
5. follow repeated cycle peaks and robustly combine their period estimates;
6. reject the lock when correlation or median-deviation checks fail;
7. fold complete tick/tock cycles with a trimmed mean and estimate beat error;
8. place strip events using constant-fraction timing and a periodic phase lock;
9. combine BPH fit, lock strength, period stability, SNR and jitter into
   confidence.

Rate never comes from the loudest acoustic peak. A physical contact sensor
produces several resonances for each escapement event, and their relative
amplitudes change from beat to beat. Long-baseline periodic correlation stays
locked to the repeated pattern instead of allowing those resonances to move
the timestamp.

Analysis runs through Qt Concurrent in the desktop application. Audio capture
and painting remain responsive while the platform-independent C++ core works
on an immutable sample snapshot.

## Synthetic laboratory source

The built-in simulator produces repeatable:

- standard and low BPH rates;
- positive or negative daily rate;
- alternating intervals for known beat error;
- three distinct damped pulse clusters per beat;
- optionally changing dominance among those three impulse clusters;
- white noise and low-level 50 Hz interference;
- optional missing impulses.

Synthetic signals prove that calculations recover known input parameters and
exercise the complete application. They cannot prove how a physical sensor,
case geometry or real escapement will behave.

## Measurement definitions

- `measured_bph = 7200 / correlated_tick_tock_cycle_seconds`
- `rate_s_per_day = (measured_bph / nominal_bph - 1) * 86400`
- beat error is the displacement of the tick/tock separation from the ideal
  half-cycle, estimated on the robust folded waveform.

## Why amplitude is not enabled yet

Balance amplitude cannot be inferred reliably from simple tick spacing. It
requires identification of acoustic escapement phases and the movement's lift
angle. Showing a value before this detector is validated would be misleading.

The next implementation will:

- segment unlock, impulse and drop features inside each beat;
- reject beats with ambiguous phase structure;
- calculate amplitude only with a known lift angle;
- compare results against a commercial reference instrument over multiple
  movements, amplitudes and positions.

## Validation protocol

Every recorded sample used for validation should include:

- movement/calibre;
- nominal BPH;
- lift angle and its source;
- position;
- reference instrument and its readings;
- ChronoLab input device, sample rate and OS audio settings;
- at least 60 seconds of unprocessed mono WAV.
