# ChronoLab

ChronoLab is a modern, cross-platform, open-source timegrapher for mechanical
watches. It listens to an acoustic or contact sensor and estimates:

- beat frequency (automatic or manual);
- rate in seconds per day;
- beat error;
- signal-to-noise ratio, interval jitter and measurement confidence;
- a traditional time-strip trace.

The project is intentionally built from scratch. No source code from `tg` is
included.

> **Current status:** 0.1 engineering preview. Rate, BPH and beat-error
> detection are implemented and tested with synthetic signals. Amplitude is
> visible in the interface but intentionally withheld until the acoustic phase
> detector has been validated against real recordings and a reference
> timegrapher. ChronoLab prefers an honest ellipsis to a convincing fake number.

## Supported inputs

ChronoLab uses the raw audio inputs exposed by the operating system:

- USB timegrapher/contact microphones;
- external USB audio interfaces;
- built-in microphone or line input;
- WAV recordings (PCM 16/24/32-bit or Float32).

The USB sensor sold under ASIN `B0DZN3HXR8` is expected to appear as a normal
USB microphone and is the first target device.

## Build with Qt Creator on Windows

Requirements:

- Qt 6.5 or later with **Qt Multimedia**;
- CMake 3.21 or later;
- a 64-bit MSVC or MinGW kit.

Steps:

1. Open `CMakeLists.txt` in Qt Creator.
2. Select a Desktop Qt 6.x kit.
3. Configure the project.
4. Build the `ChronoLab` target.
5. Run it and select the USB sensor under **Ingresso**.

The analyzer tests can be built without Qt:

```bash
g++ -std=c++20 -O2 -Isrc \
    src/core/TimegrapherAnalyzer.cpp tests/test_analyzer.cpp \
    -o chronolab_core_tests
./chronolab_core_tests
```

## Getting a clean signal on Windows

Open the USB microphone properties and disable:

- audio enhancements;
- automatic gain control;
- noise suppression;
- echo cancellation.

Start at a moderate input level. If ChronoLab's level indicator turns red, the
signal is clipping and the input level should be reduced.

## Architecture

```text
USB sensor / WAV
        |
        v
Qt Multimedia capture -> normalized mono samples
        |
        v
C++20 DSP core -> events -> BPH / rate / beat error / quality
        |
        v
Qt Widgets UI -> strip trace / waveform / CSV / WAV
```

The DSP library does not depend on Qt. This makes it deterministic, testable
and reusable by future ESP32 or command-line frontends.

## Roadmap

- Validate the detector with recordings from the target USB sensor.
- Add unlock/impulse/drop phase detection and amplitude.
- Add movement profiles and lift-angle database.
- Add guided six-position sessions and before/after comparisons.
- Add long-term stability plots and PDF reports.
- Package signed installers for Windows, macOS and Linux.

See [docs/ALGORITHM.md](docs/ALGORITHM.md) and
[docs/HARDWARE.md](docs/HARDWARE.md) for technical details. The exact Windows
workflow is in [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md).

## License

ChronoLab is licensed under the GNU General Public License v3.0 or later.
Contributions remain free and open under the same terms.
