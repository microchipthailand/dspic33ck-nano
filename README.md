# dsPIC33CK Nano

An open-hardware breadboard board for the Microchip **dsPIC33CK256MC005**
(100 MIPS dsPIC DSC), with an **Arduino-compatible core** and a 27-lab course.

<p align="center">
  <img src="docs/images/top3d.png" height="360" alt="Top view">
  <img src="docs/images/bot3d.png" height="360" alt="Bottom view">
</p>

| | |
|---|---|
| MCU | dsPIC33CK256MC005-I/PT, 256 KB flash, 24 KB RAM, internal FRC + PLL (no crystal) |
| Power | USB-C (power only) → MCP1703A 3.3 V LDO |
| Board | 31.4 × 74.5 mm, 2 layers, 2 × 22-pin headers 0.9" apart (fits a breadboard) |
| I/O | user LED RC13, user button RC12, reset button, ICSP header J4 (PICkit/SNAP), UART header J5 |
| Software | Arduino IDE / arduino-cli platform, serial bootloader, 27 example labs |

> **Status:** ERC/DRC clean, Gerbers generated, all labs compile and many were
> checked in the MPLAB simulator. **The board has not been built or tested on
> real hardware yet.**

## Repository layout

```
arduino/                       Arduino-compatible platform + labs + docs
  hardware/dspicnano/dspic33ck/  copy this into <sketchbook>/hardware/
  dsPIC33CK_Nano_Labs.html       step-by-step lab guide with wiring diagrams
  dsPIC33CK_Nano_Pinout.html     pinout page
  tools/                         doc / wiring-diagram generators and checks
pcb/
  kicad/                         KiCad 10 project (schematic, routed PCB, BOM, reports)
  scripts/                       Python generators (design.py = parts and nets)
  fabrication/                   Gerbers + drill (zip ready for the fab), BOM, pick-and-place, PDFs
docs/images/                   renders used in this README
LICENSE  NOTICE.md  LICENSES/  licensing and third-party notices
```

## Quick start (Arduino)

1. Install **MPLAB XC-DSC v4.00+** (the free edition is enough) and **MPLAB X**
   (it provides IPE for PICkit uploads) from microchip.com.
2. In the Arduino IDE, open *File → Preferences → Additional boards manager URLs*
   and add:
   ```
   https://raw.githubusercontent.com/microchipthailand/dspic33ck-nano/main/package_dspicnano_index.json
   ```
3. Open *Tools → Board → Boards Manager*, search **dsPIC33CK Nano**, and click
   **Install**. The dsPIC33CK device packs are installed with it.
   (If you are developing the core, copy `arduino/hardware/` into your sketchbook
   instead. Do not use both methods at once.)
4. Pick *Tools → Board → dsPIC33CK Nano (dsPIC33CK256MC005)*, then open
   *File → Examples → dsPIC33CK_Nano → Lab01_Blink* and upload it over a PICkit on J4.

More detail: [`arduino/README.md`](arduino/README.md) and the HTML lab guide.

## Ordering the PCB

Upload `pcb/fabrication/dspic33ck_nano_gerbers.zip` to any PCB fab. Use these
settings: 2 layers, 1.6 mm, red mask, HASL. The full spec is in
[`pcb/fabrication/README.txt`](pcb/fabrication/README.txt).

> The top silkscreen has a Microchip logo. Remove it before selling boards; see
> [`NOTICE.md`](NOTICE.md).

## License

- Original work in this repository (variant, bootloader, labs, docs, PCB design):
  **MIT**, see [`LICENSE`](LICENSE).
- Parts of the Arduino core come from
  [Arduino_dsPIC33CK](https://github.com/9Nicotin/Arduino_dsPIC33CK). The
  upstream project had no license file when this repository was made. See
  [`NOTICE.md`](NOTICE.md) for exactly which files are affected.
- Microchip linker scripts: Apache-2.0 ([`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt)).

## Trademarks

This is an independent, Arduino-compatible project. It is not affiliated with or
endorsed by Arduino SA. "Arduino" is a trademark of Arduino SA. Microchip,
dsPIC, MPLAB, PICkit and Curiosity are trademarks of Microchip Technology Inc.
