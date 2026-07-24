# ChronoLab

ChronoLab is a modern, cross-platform, open-source timegrapher for mechanical
watches. It listens to an acoustic or contact sensor and estimates:

- beat frequency (automatic or manual);
- rate in seconds per day;
- beat error;
- balance amplitude for a configured lift angle;
- signal-to-noise ratio, interval jitter and measurement confidence;
- a traditional time-strip trace.

The project is intentionally built from scratch. No source code from `tg` is
included.

> **Current status:** 0.3.3 real-signal preview. Rate, BPH, beat-error and
> amplitude detection are tested with synthetic signals and the first recording from the
> target USB contact microphone. Amplitude is displayed only when both
> tick/tock profiles expose three coherent lift impulses; ambiguous signals
> retain an honest ellipsis instead of a convincing fake number.

## Supported inputs

ChronoLab uses the raw audio inputs exposed by the operating system:

- USB timegrapher/contact microphones;
- external USB audio interfaces;
- built-in microphone or line input;
- WAV recordings (PCM 16/24/32-bit or Float32).

The USB sensor sold under ASIN `B0DZN3HXR8` is expected to appear as a normal
USB microphone and is the first target device.

## Test without a microphone

Choose **Simulatore** in the main window and configure BPH, rate, beat error,
amplitude, noise, duration and optional missed impulses. The generated
laboratory signal passes through the same analyzer used for USB and WAV input.

The simulator validates the software workflow; it does not replace calibration
against physical watches and a reference instrument.

## MULTI5 interface

ChronoLab includes five complete interface languages:

- Italian;
- English;
- French;
- German;
- Spanish.

Use the language selector in the header to choose the interface language.
ChronoLab remembers the selection and applies it on every following launch. On
first launch it follows the operating-system language when supported and
otherwise falls back to Italian.

Translations are stored as UTF-8 JSON catalogs under `translations/`, embedded
in the executable and processed entirely offline. New translations can be
contributed without changing the DSP core.

## Positional sessions

The default session matches the target holder and records two supported
positions:

- dial up (`Quadrante in alto`);
- caseback up (`Fondello in alto`).

An optional six-position mode can be enabled for users with another holder.
It is disabled by default and is not required to complete a session.

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
5. Run it, select your language and choose the USB sensor under **Ingresso**.

The analyzer tests can be built without Qt:

```bash
g++ -std=c++20 -O2 -Isrc \
    src/core/SyntheticWatch.cpp src/core/TimegrapherAnalyzer.cpp \
    tests/test_analyzer.cpp \
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
C++20 DSP core -> correlation lock -> temporal guard -> BPH / rate / beat error / quality
        |
        v
Qt Widgets UI -> strip trace / waveform / CSV / WAV
```

The DSP library does not depend on Qt. This makes it deterministic, testable
and reusable by future ESP32 or command-line frontends.

## Roadmap

- Validate the detector with recordings from the target USB sensor.
- Validate and refine amplitude over multiple movements and positions.
- Add movement profiles and lift-angle database.
- Add guided six-position sessions and before/after comparisons.
- Add long-term stability plots and PDF reports.
- Package signed installers for Windows, macOS and Linux.

See [docs/ALGORITHM.md](docs/ALGORITHM.md) and
[docs/HARDWARE.md](docs/HARDWARE.md) for technical details. The first USB
sensor comparison is documented in
[docs/REAL_SIGNAL_VALIDATION.md](docs/REAL_SIGNAL_VALIDATION.md). The exact
Windows workflow is in [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md).

## License

ChronoLab is licensed under the GNU General Public License v3.0 or later.
Contributions remain free and open under the same terms.
