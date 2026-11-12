# Notices, third-party code and trademarks

## 1. Upstream project: Arduino_dsPIC33CK (by 9Nicotin)

The Arduino core in `arduino/hardware/dspicnano/dspic33ck/` is derived from
**Arduino_dsPIC33CK 1.0.6**, <https://github.com/9Nicotin/Arduino_dsPIC33CK>,
written for Microchip's EV08P02A dsPIC33CK Curiosity Nano.

> **License status: open.** When this repository was prepared (2026-11), the
> upstream repository had **no LICENSE file**. Its README only says it is
> "provided for educational and development purposes". With no license, the
> upstream author keeps all rights to their files. The MIT license in
> [`LICENSE`](LICENSE) does **not** cover those files.
>
> TODO before or right after publishing: ask the upstream author to add a license
> (MIT preferred), or to give written permission to redistribute. Record the
> answer here.

Files taken **unchanged** from upstream (copyright: upstream author):

```
cores/arduino/   Arduino.h  HardwareSerial.c/.h  main.c  system_config.c/.h
                 wiring_digital.c  wiring_interrupts.c  wiring_private.h
                 wiring_shift.c  wiring_tone.c
libraries/       HRPWM/  SPI/  Wire/
tools/           bin/*.bat  ipecmd-upload.bat  suppress-stderr.bat
                 xc-dsc-find.bat  xc-dsc-link.bat  xc-dsc-size-wrapper.bat
platform.local.txt.template
```

Files **modified** from upstream (upstream author + changes in this repository):

```
boards.txt  platform.txt  programmers.txt  tools/serial_upload.py
cores/arduino/wiring.c         (INTCON2bits.GIE = 1 in init())
cores/arduino/wiring_analog.c  (ADC CLKDIV = 3 at 100 MIPS)
```

**New** in this repository (MIT, see `LICENSE`):

```
variants/dspic33ck_nano/         pin map for this board
bootloaders/dspic33ck_nano/      serial bootloader source + prebuilt .hex
libraries/NanoSerial2/           UART2 library
libraries/dsPIC33CK_Nano/        Lab01..Lab27 examples
arduino/*.html, arduino/tools/   lab guide, pinout page, generators
pcb/                             KiCad design, generator scripts, fabrication files
```

## 2. Microchip Technology Inc.

- `arduino/hardware/dspicnano/dspic33ck/ldscripts/p33CK256MC005-app.gld` and
  `p33CK256MC005-boot.gld` are (c) 2023- Microchip Technology Inc. and licensed
  under the **Apache License 2.0**. The full text is in
  [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt). The original headers are
  kept unchanged.
- The **MPLAB XC-DSC compiler**, **MPLAB X / IPE** and the **Device Family Packs**
  are NOT included. Install them from microchip.com under Microchip's license
  terms. The XC-DSC license does not allow redistribution.
- The prebuilt bootloader `.hex` was built with XC-DSC. Microchip's runtime
  libraries linked into it are covered by the XC-DSC license, which allows this
  kind of use.
- The PCB top silkscreen has the **Microchip logo** (`pcb/fabrication/gerber/*F_Silkscreen*`).
  It is a Microchip trademark. It is fine on personal or internal boards. Get
  Microchip's permission before you sell or hand out boards that carry it, or
  remove the logo first (`pcb/scripts/add_logo.py` adds it; the logo image is not
  in this repository).

## 3. Arduino

This is an independent, **Arduino-compatible** project. It is not affiliated with,
sponsored by or endorsed by Arduino SA. No source code from the official Arduino
cores (ArduinoCore-avr / -API, LGPL-2.1) is included. Functions such as
`digitalWrite()`, `Serial`, `Wire` and `SPI` are written from scratch, with the
same names so that sketches stay compatible.

"Arduino" and the Arduino logo are trademarks of Arduino SA. They are used here
only to describe compatibility.

## 4. Other trademarks

Microchip, dsPIC, MPLAB, PICkit and Curiosity are trademarks of Microchip
Technology Inc. KiCad is a trademark of the KiCad project. All other trademarks
belong to their owners.

## 5. Tools used (not included)

KiCad 10 (GPL-3.0), Freerouting (GPL-3.0) and arduino-cli (GPL-3.0) were used
to make the files in this repository. Their output files are not covered by
those licenses.
