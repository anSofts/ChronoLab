# Windows build and first hardware test

## Qt Creator

1. Install/select Qt 6.9.2 with:
   - MSVC 2022 64-bit or MinGW 64-bit;
   - Qt Multimedia;
   - CMake and Ninja.
2. In Qt Creator choose **File > Open File or Project**.
3. Open the top-level `CMakeLists.txt`.
4. Select the Desktop Qt 6.9.2 kit and configure.
5. In the target selector choose `ChronoLab`, not
   `chronolab_core_tests`.
6. Under **Projects > Deploy Settings**, keep
   **Install Application Manager Package** disabled.
7. Build and run.

The application can be tested immediately without the USB sensor by selecting
**Simulatore**. Start with 21,600 A/h, +8 s/day and 0.40 ms.

Run the `chronolab_core_tests` target once as well. Its console must end with:

```text
All ChronoLab analyzer tests passed.
```

## Deployable folder

From the Qt command prompt, after a Release build:

```powershell
$build = "C:\path\to\ChronoLab\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Release"
$deploy = "C:\path\to\ChronoLab\deploy"

New-Item -ItemType Directory -Force $deploy
Copy-Item "$build\ChronoLab.exe" $deploy
windeployqt --release "$deploy\ChronoLab.exe"
```

The exact build directory is shown by Qt Creator under **Projects > Build
Settings**. Do not pass `--no-translations`: ChronoLab's five application
catalogs are embedded, while the Qt translation files deployed by
`windeployqt` localize standard buttons and system dialogs.

## Target USB sensor checklist

Before opening ChronoLab:

1. connect the sensor directly to a USB port;
2. open **Settings > System > Sound > Input**;
3. speak/tap very gently on the holder and confirm that its level moves;
4. open the device's additional properties;
5. disable enhancements, AGC, echo cancellation and noise suppression;
6. note every selectable format under **Advanced**.

Inside ChronoLab:

1. select the sensor under **Ingresso**;
2. leave A/H on **Automatico**;
3. select the movement's correct lift angle;
4. click **Avvia ascolto**;
5. fit the watch firmly without touching the cable;
6. wait at least 15 seconds;
7. save the raw WAV before changing position.

The standard positional session contains only **Quadrante in alto** and
**Fondello in alto**, matching the target holder. The six-position checkbox is
optional and intended for different or future holders.

If the level stays red, reduce the Windows input level. If it barely moves,
increase the input level or improve physical contact before changing software
filters.
