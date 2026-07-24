# First real-signal validation

ChronoLab 0.3.1 was developed against the first recording captured with the
target USB contact microphone.

## Input

- PCM16 mono WAV;
- 48,000 Hz;
- 30.0 seconds;
- peak level 0.266 FS;
- RMS level 0.0194 FS;
- no clipped samples;
- analyzer SNR approximately 13.3 dB;
- nominal rate 21,600 A/h;
- lift angle 52 degrees.

The reference TG measurement was made separately on the same watch and showed:

- rate: -9 s/day;
- beat error: 0.9 ms;
- amplitude: 278 degrees;
- frequency: 21,600 A/h.

This is a same-watch comparison, not a simultaneous calibrated laboratory
measurement.

## Failure reproduced in 0.3.0

The old peak-based analyzer changed sign and moved by tens or hundreds of
seconds per day as its 18-second window advanced. Some windows seeded the event
chain with a false resonance and generated four-digit internal rate errors.

## 0.3.1 result

Thirteen consecutive overlapping 18-second windows from the same WAV produced:

- rate range: -10.29 to -10.80 s/day;
- beat-error range: 0.83 to 0.89 ms;
- jitter range: 0.57 to 0.68 ms;
- confidence range: 84 to 86 percent;
- no BPH changes, unlocks or rate spikes.

The purpose of this fixture was to prove repeatable signal lock. More watches,
positions, amplitudes and reference instruments are still required before
claiming metrological accuracy.

## 0.3.2 follow-up

The original beat-error correlation added tick-to-tock and inverse
tock-to-tick comparisons. Their opposite offsets could cancel and produce a
false 0.00 ms result. Version 0.3.2 uses only the directional comparison.

On the same thirteen windows it produced:

- raw rate range: -10.29 to -10.80 s/day;
- displayed median-filtered rate range: -10.30 to -10.58 s/day;
- beat-error range: 0.95 to 0.98 ms;
- no BPH changes, unlocks or rate spikes.

The desktop layer now also withholds an isolated rate/BPH jump and requires
three mutually coherent windows before treating a large change as real.
