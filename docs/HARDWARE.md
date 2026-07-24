# Target USB sensor

The first target is the compact USB contact sensor sold under ASIN
`B0DZN3HXR8`, model `500766070`.

Published listings describe:

- an integrated vibration sensor and preamplifier;
- ceramic filtering;
- internal foam/noise isolation;
- desktop, laptop, tablet and Android compatibility;
- enumeration as a USB microphone under Windows 11.

Marketing specifications do not document the ADC, native sample rate, USB
vendor/product IDs or transfer characteristics. ChronoLab therefore discovers
the formats exposed by the operating system and prefers, in order:

1. 48 kHz, mono, signed 16-bit;
2. 44.1 kHz, mono, signed 16-bit;
3. mono Float32 or signed 32-bit;
4. the device's preferred format, downmixed if necessary.

## First-device characterization

When the physical unit is available, record:

- Windows device description;
- Hardware IDs from Device Manager;
- every format offered by Advanced audio properties;
- whether enhancements/AGC are available and their defaults;
- 60–90 second raw WAV samples from 14,400, 18,000, 21,600 and 28,800 A/h
  movements where possible;
- a silence/no-watch recording;
- a clipping test and a deliberately weak-contact recording.

No firmware or custom driver is required for the initial implementation.
