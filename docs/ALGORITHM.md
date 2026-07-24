# Analyzer design

ChronoLab 0.3.5 uses a deterministic signal-processing pipeline:

1. generate an RMS envelope and adaptive transient candidates for BPH scoring;
2. isolate the sharp escapement content with independent second-order filters;
3. rectify and downsample the pulse signal to a safe analysis rate;
4. calculate long-baseline autocorrelation with an internal radix-2 FFT;
5. follow repeated cycle peaks and robustly combine their period estimates;
6. reject the lock when correlation or median-deviation checks fail;
7. fold complete tick/tock cycles with a trimmed mean and estimate beat error
   using directional tick-to-tock correlation;
8. place strip events using constant-fraction timing and a periodic phase lock;
9. build robust tick and tock profiles, identify their three lift impulses and
   calculate amplitude only when both profiles agree;
10. measure SNR on the escapement pulse band instead of broadband room audio;
11. combine BPH fit, lock strength, period stability, SNR and jitter into
   confidence;
12. retain a time-based history of overlapping measurements and smooth
    confidence independently of the machine's analysis throughput;
13. preserve the last locked measurement during brief rejected windows,
    decrease confidence gradually and release the lock only after a sustained
    loss;
14. require a coherent candidate cluster over time before accepting a large
    rate or BPH change;
15. stabilize beat error independently over eight seconds, reject an isolated
    collapse and accept a new level only after a coherent temporal cluster;
16. calculate robust beat-error dispersion from the unfiltered observations so
    genuine instability remains visible even while the numerical median is
    protected.

Rate never comes from the loudest acoustic peak. A physical contact sensor
produces several resonances for each escapement event, and their relative
amplitudes change from beat to beat. Long-baseline periodic correlation stays
locked to the repeated pattern instead of allowing those resonances to move
the timestamp.

Analysis runs through Qt Concurrent in the desktop application. Audio capture
and painting remain responsive while the platform-independent C++ core works
on an immutable sample snapshot. The live-data loop polls at 33 ms
(approximately 30.3 Hz); heavy DSP jobs are backpressured so only one immutable
snapshot is analyzed at a time.

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
  half-cycle, estimated with one-direction tick-to-tock correlation so the
  inverse comparison cannot cancel the offset.
- displayed beat error is the median of accepted observations over eight
  seconds. Its `±` value is `1.4826 * median(abs(x - median(x)))`, calculated
  from raw observations. Dispersion above 0.20 ms is reported as unstable.
- `amplitude_deg = 3600 * lift_angle_deg / (pi * nominal_bph * lift_time_s)`
  where lift time is the robust first-to-third impulse separation.

## Amplitude validation policy

Balance amplitude cannot be inferred from simple tick spacing. It requires
identification of acoustic escapement phases and the movement's correct lift
angle. ChronoLab calculates separate tick and tock profiles, requires three
peaks in each, rejects physically implausible results and requires the two
directions to agree within 60 degrees.

Future validation will:

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
