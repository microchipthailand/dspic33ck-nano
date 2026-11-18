# dsPIC33CK Nano - peripheral labs 31-38

Eight more labs for the dsPIC33CK Nano board. Each one covers a peripheral that
most small microcontrollers do not have. They continue the numbering of the 30
labs in the `dsPIC33CK_Nano` examples.

In the Arduino IDE they are under **File > Examples > dsPIC33CK_Nano_Peripherals**.
The step-by-step lab manual is `dsPIC33CK_Nano_Peripheral_Labs.html` in the top folder,
next to the main guide.

Each lab keeps its driver code in a `.h` tab next to the sketch, so the `.ino`
stays short. Every sketch has a full header with the goal, wiring, commands and
the theory. All of them use the Serial Monitor on J5 at 115200 baud.

| Lab | Peripheral | Driver tab | Wiring | Clock |
|-----|------------|------------|--------|-------|
| 31 AnalogComparator | CMP1 + 12-bit DAC threshold: hysteresis, PPS output, edge counter, SAR ADC built from DAC + comparator | `Comparator.h` | pot on A0 (J2-15); jumper D8 to D7 (J3-16 to J3-17) for the frequency test | 100 MIPS only |
| 32 PPS | Peripheral Pin Select: move a PWM and an interrupt input between pins, virtual pins RPV0/RPV1 (frequency and UART loop-back with no wire), IOLOCK, full pin-map dump | `PPS.h` | none (optional jumper on D8 J3-16) | both |
| 33 PTG | Peripheral Trigger Generator: a step program that blinks the LED, reacts to the button, steps on a software trigger, and makes 1 kHz on D9, all with zero CPU time | `PTG.h` | none (optional scope on D9 J3-15) | both |
| 34 CRC | Hardware CRC engine: CRC-8/16/32 catalogue self-test, speed against software, Modbus frame check, flash fingerprint (same CRC-32 as the serial bootloader) | `CRC.h` | none | both |
| 35 DeadmanTimer | DMT: an instruction-count watchdog with a window. Too late, too early and wrong key are each caught and reported after the reset. Also used as an instruction counter | `DMT.h` | none | both |
| 36 DSPMath | Q15 fixed point, 40-bit accumulators, saturation, MAC; FIR benchmark (float / int32 / MAC / REPEAT+MAC); FIR to the Serial Plotter; RMS; Goertzel DTMF decoder; live A0 filter | `DSP.h` | none (pot on A0 for the live filter) | both (100 MIPS recommended) |
| 37 HighResPWM | 6 independent PWM channels (duty + phase each) from PG1-PG3 at the finest resolution the MC005 has: FVCO/2 = 400 MHz clock (2.5 ns), plus dithering to 1/16 tick; resolution table, 6-phase interleave, 6 breathing LEDs, self-measured clock | `PWM6.h` | scope or 6 LEDs + 1k on J2-4..J2-9 | 100 MIPS only |
| 38 AdvancedADC | The whole 12-bit ADC: all 22 channels (20 pins + temperature + band gap) from one trigger, 6/8/10/12-bit resolution, sample time and speed, timer- and PWM-triggered sampling with an interrupt callback, continuous conversion, digital comparator window, oversampling (13-16 bit) and averaging filters, signed / differential / fractional formats, AVDD from the band gap | `ADC.h` | pot on A0 (J2-15); optional 1.65 V divider on RD13 (J2-13) | both |

## Test status

None of these labs has been run on hardware yet, because the board has not been
built. Each one was compiled at 100 MIPS, at 4 MIPS and with the serial
bootloader, with no warnings. Labs 31 and 37 stop with an `#error` at 4 MIPS on
purpose, because they need the PLL.

| Lab | MPLAB simulator |
|-----|-----------------|
| 31 | registers only (the simulator has no analog comparator) |
| 32 | registers only: every RPOR/RPINR write lands as expected and the pin-map dump is right, but the simulator does not route signals through PPS (no pin edges, no virtual-pin loop-back) and does not enforce IOLOCK |
| 33 | **not modelled**: the simulator has no PTG. The step encodings come from Microchip's MCC device data for this chip |
| 34 | **verified**: all 7 catalogue CRCs match the standard check values, and the speed test, Modbus frame and flash CRC all work |
| 35 | **not modelled**: the simulator has no DMT, so its counter stays at 0. The sketch prints the real config words and counter, so the chip shows what it does |
| 36 | **verified**: Q15, saturation, benchmark (all four filter versions give the same output), RMS and DTMF decode. The live ADC part is not verified, because the simulator has no ADC |
| 37 | registers only: period, phase/duty/TRIGA/TRIGB of all six channels, the clock divider and the overrides are written as intended. The simulator has no PWM model. The board's MC005 has **no true 250 ps high-resolution PWM** (that is an MP-family feature). This lab uses the fastest PWM clock plus dithering, and measures the real clock at start-up |
| 38 | registers only: per-channel triggers, signed/differential bits, comparator and filter settings, interrupt enables, resolution, SCCP3 and PWM trigger are written as intended and the shared core powers up, but the simulator has no ADC model (no conversions). The sketch itself tests which timer-trigger code works and whether the common ADC interrupt fires |

Things found while testing that are now handled in the code:
- `analogRead()` of the core returns 10 bits (0-1023). Labs 31, 36 and 37 had
  scaled it as 12 bits (/4096); they now divide by 1024. Lab 38's `adcRead()`
  gives the full 12 bits.
- CRC: in this engine's legacy mode the data has to be followed by zero bytes,
  and the start value has to be converted to the "indirect" seed first.
  `crcHw()` does both. The `a` command tests whether the alternate mode
  (MOD = 1) is a plain direct CRC on a real chip. In the simulator it is not.
- DSP: the accumulator must be declared `volatile register int acc asm("A")`.
  Without `volatile` the compiler stops with "Expected DSP accumulator result".
  A MAC builtin inside a C loop is no faster than plain C (about 10 cycles per
  tap). `REPEAT` plus `MAC` with prefetch takes about 1 cycle per tap. It needs
  the second operand in Y memory (0x3000-0x4FFF, `DSP_YMEM`).
- DMT: config words are programmed only by a PICkit upload (Bootloader: None).
  After a serial-bootloader upload they stay erased, the window opens only at
  the time-out, and the sketch says so.
