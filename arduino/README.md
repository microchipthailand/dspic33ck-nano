# Arduino core for the dsPIC33CK Nano board

Arduino support for the custom **dsPIC33CK Nano** breadboard board
(dsPIC33CK256MC005, USB-C power, 2 × 22 pins, red PCB). It is derived from
[Arduino_dsPIC33CK](https://github.com/9Nicotin/Arduino_dsPIC33CK) 1.0.6, which
targets Microchip's EV08P02A Curiosity Nano. The chip and the core code are the
same. Only the parts that depend on the board wiring are different:

| | EV08P02A Curiosity Nano | dsPIC33CK Nano (this) |
|---|---|---|
| User LED | RD10 (D37), active low | **RC13 (D34)**, active low |
| User button | RD13 (D38), internal pull-up | **RC12 (D33)**, external 10 k pull-up |
| Reset button | none | **SW1 on MCLR** |
| Programmer | on-board nEDBG | **PICkit 4/5 or SNAP on J4** (PGD3/PGC3) |
| Serial | nEDBG USB-CDC on RC10/RC11 | **J5 header** on RC10/RC11 + USB-serial adapter |
| SPI MISO / SS | RC12 / RC13 | **RC8 (D29) / RC9 (D30)**, freed for LED/button |
| Bootloader | watches RD13, LED RD10 | **watches RC12, LED RC13**, rebuilt |

The D0–D38 / A0–A19 numbering is the same as the original core, so its
pin tables stay valid.

## Contents

```
arduino/
├── dsPIC33CK_Nano_Pinout.html     Arduino-style pinout + board guide (open in a browser)
├── dsPIC33CK_Nano_Labs.html       step-by-step guide for Lab01..Lab27 (open in a browser)
├── hardware/dspicnano/dspic33ck/  the Arduino platform (copy into <sketchbook>/hardware/)
│   ├── boards.txt                 board "dsPIC33CK Nano", Clock / Debugger / Bootloader menus
│   ├── variants/dspic33ck_nano/   pins_arduino.h + variant.c  (pin map)
│   ├── bootloaders/dspic33ck_nano/  bootloader source, build script, prebuilt .hex
│   └── libraries/dsPIC33CK_Nano/examples/Lab01..Lab27
├── tools/gen_pinout_html.py       regenerates the pinout page (checks it against the core)
├── tools/gen_wiring.py            breadboard wiring diagram (SVG) for every lab, pins from the PCB netlist
└── tools/embed_wiring.py          puts those diagrams into dsPIC33CK_Nano_Labs.html (re-runnable)
```

## Install

1. Install **XC-DSC v4.00+** and **MPLAB X** (for ipecmd).
2. **Boards Manager (recommended):** in *File → Preferences → Additional boards
   manager URLs* add
   `https://raw.githubusercontent.com/microchipthailand/dspic33ck-nano/main/package_dspicnano_index.json`,
   then go to *Tools → Board → Boards Manager → "dsPIC33CK Nano" → Install*. This
   also installs the device packs `dsPIC33CK-MC_DFP` / `MP_DFP` (pruned Microchip
   DFPs hosted by the upstream Arduino_dsPIC33CK v1.0.6 release).
   **Manual (core development):** copy `hardware/` into your sketchbook folder
   (*File → Preferences → Sketchbook location*), so the path is
   `<sketchbook>/hardware/dspicnano/dspic33ck/boards.txt`, then restart the IDE.
   The DFP tools still have to come from a Boards Manager install (this one or
   upstream's). Do not keep both installs at once.
3. *Tools → Board → **dsPIC33CK Nano (dsPIC33CK256MC005)***.
   *Tools → Debugger →* the programmer plugged into J4.
4. Open *File → Examples → dsPIC33CK_Nano → Lab01_Blink* and upload.

### Serial upload over J5 (optional)

Burn the bootloader once with the PICkit (*Tools → Burn Bootloader*). Then set
*Tools → Bootloader → Serial over J5* and pick the COM port of the 3.3 V
USB-serial adapter on J5 (adapter RX → J5-2, TX → J5-3, GND → J5-4).
For manual entry: hold USER, tap RESET, and keep holding USER for about 1 s.

## Labs

| # | Sketch | Topic | Extra parts |
|---|---|---|---|
| | **Basics** | | |
| 01 | Lab01_Blink | digital output | – |
| 02 | Lab02_ButtonLED | digital input, debounce | – |
| 03 | Lab03_SerialHello | Serial print / commands | USB-serial on J5 |
| 04 | Lab04_AnalogRead | ADC, volts | 10 k pot on A0 (J2-15) |
| 05 | Lab05_PWMFade | analogWrite, map | LED + 330 Ω on D5 (J3-21) |
| 06 | Lab06_Interrupt | attachInterrupt, volatile | – |
| 07 | Lab07_Tone | tone(), arrays | piezo on D9 (J3-15) |
| 08 | Lab08_I2CScanner | Wire | any 3.3 V I2C module |
| 09 | Lab09_SPILoopback | SPI | jumper J3-5 ↔ J3-14 |
| | **Advanced** | | |
| 10 | Lab10_PinChange | Change Notification on many pins (RISING/FALLING/CHANGE), rotary encoder, INT1 | buttons, encoder, jumper |
| 11 | Lab11_Timers | Timer1, SCCP1-3 + PG4 periodic interrupts, prescalers, 32-bit mode, latency | LED/piezo optional |
| 12 | Lab12_UARTInterrupt | UART3 driver: RX/TX/error interrupts, ring buffers | jumper J2-14 ↔ J2-20 |
| 13 | Lab13_SoftwareSerial | timer ISR UART (SCCP3) | jumper J2-18 ↔ J2-19 |
| 14 | Lab14_DMA | timer → ADC → DMA ping-pong, mem copy | pot on A2 (J2-17) |
| 15 | Lab15_DAC | 12-bit DAC on RA3, waveforms, comparator CMP1 (100 MIPS) | jumper J2-18 ↔ J2-17, pot |
| 16 | Lab16_AdvancedPWM | PG1-PG3 complementary PWM, dead time, 3-phase sine (100 MIPS) | scope / LEDs on J2-4..J2-9 |
| 17 | Lab17_EEPROM | EEPROM emulation in Flash, wear levelling, CRC | – |
| 18 | Lab18_PowerReset | RCON reset reasons (POR/BOR/MCLR/WDT/SW), Doze, Idle, Sleep | – |
| 19 | Lab19_RTOS | preemptive mini-RTOS: tasks, context switch, semaphore | – |
| | **Application** | | |
| 20 | Lab20_DataLogger | everything: CSV logger + alarm | pot, LDR, buzzer |
| 21 | Lab21_GPSLogger | Serial2 (UART2), NMEA parsing, checksum | 3.3 V GPS: TX → J2-4 |
| 22 | Lab22_WS2812B | SPI as a waveform generator (100 MIPS only) | WS2812B strip on MOSI J3-5 |
| 23 | Lab23_OLED | SSD1306 128x64 I2C driver, frame buffer, font | OLED on J3-7/J3-8 |
| 24 | Lab24_ModbusRS485 | Modbus RTU master/slave, CRC-16 | 2 boards + RS485 modules (J2-4/5/6) |
| 25 | Lab25_LIN | LIN master/slave, break/sync/PID/checksum on UART2 | jumper J2-5 ↔ J2-4 or LIN transceiver |
| 26 | Lab26_CAN | CAN FD controller, 500 kbit/s, loopback + real bus (100 MIPS) | CAN transceiver on J2-22/J3-18 |
| 27 | Lab27_10BaseT1S | 10BASE-T1S Ethernet via LAN8651 MAC-PHY (TC6 over SPI) | Two-Wire ETH Click |

Labs are grouped **Basics 01-09 -> Advanced 10-19 -> Application 20-27**.
Full step-by-step explanations, each with a breadboard wiring diagram:
`dsPIC33CK_Nano_Labs.html`. To redraw the diagrams after a pin change:

```
python tools/gen_wiring.py ../pcb/scripts wiring_out
python tools/embed_wiring.py dsPIC33CK_Nano_Labs.html wiring_out/wiring.json
```

(KiCad's python works; no extra packages.) Every wire names a signal. The
header pin is looked up in `pcb/scripts/design.py`, so a wire can't end up on
a pin that doesn't carry that signal.

## Publishing a new Boards Manager release

1. Bump `version=` in `hardware/dspicnano/dspic33ck/platform.txt`.
2. Run `python arduino/tools/make_release.py`. It writes
   `dist/dspicnano-dspic33ck-<ver>.zip` and updates `package_dspicnano_index.json`
   (URL, SHA-256, size).
3. On GitHub, create release **v<ver>** and attach the zip.
4. Commit and push `package_dspicnano_index.json` to `main`.

Keep older entries in the index if you want users to be able to downgrade.
(The script currently writes only the current version.)

## Verification status (2026-11-11)

- The original ten labs (now 01-09 and 20) compile with 0 warnings with arduino-cli 1.5.1 and XC-DSC v4.00,
  in 3 configurations: 100 MIPS, 4 MIPS, and Serial-bootloader link.
- Labs 21, 22, 13, 24, 14 compile at 100 MIPS and 4 MIPS (Lab22 is meant to fail at 4 MIPS:
  its `#error` asks for 100 MIPS). Lab14's DMA/ADC trigger codes come from the
  MCC device data (Microchip MCC JSON, not included). Which SCCP2 event actually triggers the ADC
  was not checked on hardware, so the sketch tries several and prints the one
  it ended up using.
- Labs 17, 16, 12, 19, 23 compile at 100 MIPS, 4 MIPS and with the Serial bootloader
  (Lab16 is meant to fail at 4 MIPS, like Lab22). They were also run in the
  MPLAB X simulator (`mdb`):
  - Lab17: the boot counter survives 3 resets.
  - Lab19: task switching and the CPU-load figure work.
  - Lab12: UART3 TX works, but only via its polled fallback, because the
    simulator never raises the UART TX interrupt.
  - Lab16: the PWM registers are set up, but the simulator has no PWM model.
  - Lab23: the drawing code was checked by rendering the frame buffer on the PC.
- Labs 18, 15, 26, 25, 27 compile in all 3 configurations, except Labs 15/26 at 4 MIPS (an
  intended `#error`: they need the PLL). Simulator results:
  - Lab18: the POR, software and watchdog reset flags all show up correctly, and
    Idle wakes 1x per ms.
  - Lab15: the DAC registers are set up correctly, but there is no analog model.
  - Lab26: the CAN module is not simulated.
  - Lab25: the PID and checksum functions match known reference values.
  - Lab27: written from the OPEN Alliance TC6 specification and not verified
    (no hardware to test with).
- Core change: `analogRead()` now divides the ADC clock by 4 at 100 MIPS.
  Before, TAD was 10 ns, i.e. the ADC was overclocked.
- Core change: `init()` now sets `INTCON2bits.GIE = 1` (the device file gives GIE
  a reset value of 0; the simulator agrees).
- The SPI pin mapping was checked in the disassembly: RC4 SCK, RC5 MOSI, RC8
  MISO (RPINR20 = 56), RC9 SS output.
- Bootloader: builds reproducibly with `build_bootloader.sh`. It ends at 0xA9C
  (below the 0x1600 cap), jumps to the sketch at 0x1800, and its config words
  match the core's. In the disassembly it reads RC12 and drives RC13, and it
  never touches PORTD.
- Every J2/J3 position quoted in the labs and the pinout page was checked
  against the PCB netlist (`pcb/scripts/design.py`).
- **Nothing has been tested on real hardware yet.** The PCB has not been
  built. Do the first bring-up with Lab01 over the PICkit before trying the
  bootloader.

---

Arduino-compatible core. Not affiliated with or endorsed by Arduino SA.
"Arduino" is a trademark of Arduino SA. See [`../NOTICE.md`](../NOTICE.md) for
third-party code and licences.
