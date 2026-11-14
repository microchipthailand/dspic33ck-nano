# Notices, third-party code and trademarks

Unless a file says otherwise, everything in this repository is covered by the
MIT license in [`LICENSE`](LICENSE). The exceptions are listed below.

## 1. Microchip Technology Inc.

- `arduino/hardware/dspicnano/dspic33ck/ldscripts/p33CK256MC005-app.gld` and
  `p33CK256MC005-boot.gld` are (c) 2023- Microchip Technology Inc. and licensed
  under the **Apache License 2.0**. The full text is in
  [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt). The original headers are
  kept unchanged.
- The GitHub release has pruned copies of the Microchip **Device Family Packs**
  `dsPIC33CK-MC_DFP` and `dsPIC33CK-MP_DFP` (only the files the compiler needs),
  so that Boards Manager can install them. They are (c) Microchip Technology Inc.
  under the **Apache License 2.0**; each zip keeps Microchip's `LICENSE.txt`.
- The **MPLAB XC-DSC compiler** and **MPLAB X / IPE** are NOT included. Install
  them from microchip.com under Microchip's license terms. The XC-DSC license
  does not allow redistribution.
- The prebuilt bootloader `.hex` was built with XC-DSC. Microchip's runtime
  libraries linked into it are covered by the XC-DSC license, which allows this
  kind of use.
- The PCB top silkscreen has the **Microchip logo** (`pcb/fabrication/gerber/*F_Silkscreen*`).
  It is a Microchip trademark. It is fine on personal or internal boards. Get
  Microchip's permission before you sell or hand out boards that carry it, or
  remove the logo first (`pcb/scripts/add_logo.py` adds it; the logo image is not
  in this repository).

## 2. Arduino

This is an independent, **Arduino-compatible** project. It is not affiliated with,
sponsored by or endorsed by Arduino SA. No source code from the official Arduino
cores (ArduinoCore-avr / -API, LGPL-2.1) is included. Functions such as
`digitalWrite()`, `Serial`, `Wire` and `SPI` are written from scratch, with the
same names so that sketches stay compatible.

"Arduino" and the Arduino logo are trademarks of Arduino SA. They are used here
only to describe compatibility.

## 3. Other trademarks

Microchip, dsPIC, MPLAB, PICkit and Curiosity are trademarks of Microchip
Technology Inc. KiCad is a trademark of the KiCad project. All other trademarks
belong to their owners.

## 4. Tools used (not included)

KiCad 10 (GPL-3.0), Freerouting (GPL-3.0) and arduino-cli (GPL-3.0) were used
to make the files in this repository. Their output files are not covered by
those licenses.
