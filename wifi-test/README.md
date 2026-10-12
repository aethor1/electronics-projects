# ESP32 Switch and LED Test

Contains an in-progress GPIO18 switch and built-in LED experiment. Despite the original folder name, no Wi-Fi functionality is implemented.

- Board: ESP32 DOIT DevKit V1
- Framework: Arduino via PlatformIO
- Libraries: Arduino.h (framework)
- Status: Work in progress: the if-condition calls digitalWrite(), which returns no value, so this version needs correction before it can compile.
- Original folder: `Wifi Test`

Open this folder as a PlatformIO project. Dependencies are declared in `platformio.ini`.

```sh
pio run
```

The source and configuration are preserved as they were at publication.
