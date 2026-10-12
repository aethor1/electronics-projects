# Automatic Mirror Light

Queues a 90 -> 130 -> 90 degree servo movement on a BOOT button press or a PIR signal transition after a 90-second warm-up. Prints PIR state and briefly lights the status LED after warm-up.

- Board: ESP32 DOIT DevKit V1
- Framework: Arduino via PlatformIO
- Libraries: ESP32Servo 1.2.1
- Status: Prototype; actual mirror/light hardware operation has not been checked for this publication.
- Original folder: `Automatic Mirror light`

Open this folder as a PlatformIO project. Dependencies are declared in `platformio.ini`.

```sh
pio run
```

The source and configuration are preserved as they were at publication.
