# Analyzer design

ChronoLab 0.3 uses a deterministic signal-processing pipeline:

1. remove DC and low-frequency handling noise with a first-order high-pass;
2. generate a short RMS envelope;
3. estimate the noise floor with median and median absolute deviation;
4. detect impulse clusters using an adaptive threshold and refractory window;
5. score standard mechanical-watch beat rates against detected intervals;
6. assign integer beat indices while tolerating missed impulses;
7. fit time against beat index with linear regression;
8. derive rate, alternating beat error, jitter and residual strip points;
9. combine fit, event count, SNR and jitter into a confidence score.

This design avoids using only the loudest spectral peak, which can easily be a
harmonic of the actual beat frequency.

Analysis runs through Qt Concurrent in the desktop application. Audio capture
and painting remain responsive while the platform-independent C++ core works
on an immutable sample snapshot.

## Synthetic laboratory source

The built-in simulator produces repeatable:

- standard and low BPH rates;
- positive or negative daily rate;
- alternating intervals for known beat error;
- three distinct damped pulse clusters per beat;
- white noise and low-level 50 Hz interference;
- optional missing impulses.

Synthetic signals prove that calculations recover known input parameters and
exercise the complete application. They cannot prove how a physical sensor,
case geometry or real escapement will behave.

## Measurement definitions

- `measured_bph = 3600 / fitted_seconds_per_beat`
- `rate_s_per_day = (measured_bph / nominal_bph - 1) * 86400`
- beat error is the absolute difference between the mean alternating
  tick-to-tock and tock-to-tick intervals.

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
