/*
 * Lab 15 - DAC: a real analog output (12-bit) + the comparator it belongs to
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required - the DAC needs the PLL)
 *
 * Goal:  analogWrite() is only PWM. This chip has a true 12-bit DAC. Use it to
 *        (1) output DC levels and waveforms (sine, triangle, staircase),
 *        (2) read them back with the ADC to check the DAC - linearity test,
 *        (3) use it as the threshold of the analog comparator CMP1 - a
 *            hardware "is the pot above X volts?" detector with an interrupt.
 *
 * Pins:
 *   DACOUT1 = RA3 (J2-18)  <- the DAC output
 *   A2      = RA2 (J2-17)  <- ADC read-back: put a jumper J2-18 -> J2-17
 *   CMP1A   = RA0 (J2-15)  <- comparator input: pot wiper (as Lab 04)
 *
 * Wiring:
 *   - jumper J2-18 (DACOUT1) to J2-17 (A2)
 *   - optional: multimeter / scope on J2-18 to GND
 *   - optional: pot wiper -> J2-15, ends to 3V3 / GND (comparator demo)
 *
 * Commands (Serial Monitor 115200):
 *   v<mV>  DC output, e.g. v1650           s  sine 50 Hz      t  triangle
 *   c      staircase (16 steps)            l  linearity test (DAC -> ADC table)
 *   p      stream A2 for the Serial Plotter, any key stops
 *   k<mV>  comparator threshold, e.g. k2000 (then turn the pot through it)
 *
 * ----------------------------------------------------------------------------
 * HOW IT WORKS (registers from p33CK256MC005.h / MCC device data)
 *   The DAC lives inside comparator module 1 ("CMP1"):
 *     DACCTRL1L  DACON (bit 15) master on, CLKSEL = FPLLO (11), CLKDIV, FCLKDIV
 *                The DAC/comparator clock must be a fast PLL clock (~500 MHz max);
 *                at 100 MIPS FPLLO = 200 MHz. With the plain 8 MHz FRC
 *                (4 MIPS menu) there is no PLL - hence the #error below.
 *     DAC1CONL   DACEN (bit 15) this DAC on, INSEL = comparator input A..D,
 *                HYSSEL hysteresis, IRQM comparator interrupt edge
 *     DAC1DATH   the 12-bit output value: Vout = AVDD * DAT / 4096
 *     CMPSTAT    (DAC1CONL bit 7) the comparator output, 1 = input > DAC
 *   NOT verified on hardware: whether DACOUT1 is driven by DACEN alone. This
 *   device's register file shows NO separate output-enable bit, so the lab
 *   assumes it is. Check with a meter on J2-18 / the 'l' test; if RA3 stays at
 *   0 V, the DAC still works internally as the comparator threshold ('k').
 *   The DAC output is weak (high impedance, ~hundreds of uA): buffer it with
 *   an op-amp before driving a load.
 *   Waveforms: the samples are paced with micros() in loop() (no timer is
 *   used, so nothing clashes with PWM/tone). There is some jitter - fine for
 *   50 Hz demos; Lab 14 shows how DMA + a timer give zero-jitter sampling.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <math.h>

#if FCY < 50000000UL
#error "Lab15 needs Tools > Clock = 100 MIPS (the DAC is clocked from the PLL)"
#endif

const uint8_t READBACK = A2;      /* RA2, jumper from DACOUT1 (RA3) */
const double  AVDD = 3.3;

volatile unsigned long cmpEdges = 0;
int16_t sineTab[64];

/* ---------------- DAC ---------------- */
void dacBegin(void)
{
    PMD7bits.CMP1MD = 0;          /* power comparator/DAC module 1 */

    DACCTRL1L = 0;
    DACCTRL1Lbits.CLKSEL  = 3;    /* FPLLO */
    DACCTRL1Lbits.CLKDIV  = 0;    /* 1:1 */
    DACCTRL1Lbits.FCLKDIV = 0;
    DACCTRL2H = 0x8A;             /* SSTIME / TMODTIME: MCC defaults for slope mode */
    DACCTRL2L = 0x55;             /*   (unused here; written so they are defined) */

    DAC1CONL = 0;
    DAC1CONLbits.INSEL  = 0;      /* comparator input A = CMP1A = RA0 */
    DAC1CONLbits.HYSSEL = 2;      /* 30 mV hysteresis: no chatter at the threshold */
    DAC1CONLbits.IRQM   = 3;      /* interrupt on rising and falling edge */
    DAC1CONH = 0;
    DAC1DATH = 0;
    DAC1DATL = 0;
    SLP1CONH = 0;                 /* slope compensation off */
    SLP1CONL = 0;

    /* analog pins: RA3 (DACOUT1) and RA0 (CMP1A) analog, RA2 for the ADC */
    ANSELAbits.ANSELA3 = 1; TRISAbits.TRISA3 = 1;
    ANSELAbits.ANSELA0 = 1; TRISAbits.TRISA0 = 1;

    DAC1CONLbits.DACEN = 1;
    DACCTRL1Lbits.DACON = 1;

    IPC19bits.CMP1IP = 3;
    IFS4bits.CMP1IF = 0;
    IEC4bits.CMP1IE = 1;
}

inline void dacWrite(uint16_t v) { DAC1DATH = v & 0x0FFF; }      /* 0..4095 */
void dacWriteMv(long mv)
{
    if (mv < 0) mv = 0;
    if (mv > 3300) mv = 3300;
    dacWrite((uint16_t)(mv * 4095L / 3300L));
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _CMP1Interrupt(void)
{
    cmpEdges++;
    IFS4bits.CMP1IF = 0;
}

/* ---------------- helpers ---------------- */
long readNumber(void)
{
    long v = 0; unsigned long t0 = millis();
    while (millis() - t0 < 50) {
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0'); t0 = millis();
        }
    }
    return v;
}

long readbackMv(void)
{
    long acc = 0;
    for (int i = 0; i < 16; i++) acc += analogRead(READBACK);   /* average 16 */
    return acc * 3300L / (16L * 1023L);
}

void linearity(void)
{
    Serial.println("# DAC code -> expected mV -> ADC read-back mV (jumper J2-18 -> J2-17)");
    long worst = 0;
    for (int code = 0; code <= 4095; code += 455) {
        dacWrite((uint16_t)code);
        delay(5);
        long exp = (long)code * 3300L / 4095L;
        long got = readbackMv();
        long err = got - exp;
        if (labs(err) > labs(worst)) worst = err;
        Serial.print("#  "); Serial.print(code); Serial.print("\t"); Serial.print(exp);
        Serial.print("\t"); Serial.print(got); Serial.print("\terr "); Serial.println(err);
    }
    Serial.print("# worst error "); Serial.print(worst);
    Serial.println(" mV (ADC is 10-bit here = 3.2 mV/step; a few tens of mV is normal)");
    if (readbackMv() < 50) Serial.println("# read-back ~0 V: jumper missing, or DACOUT1 not driven (see header)");
}

enum Mode { DC, SINE, TRI, STAIR };
Mode mode = DC;
long dcMv = 1650;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    for (int i = 0; i < 64; i++) sineTab[i] = (int16_t)(2047.0 + 2000.0 * sin(2.0 * M_PI * i / 64.0));
    dacBegin();
    dacWriteMv(dcMv);
    Serial.println("Lab 15 - DAC on DACOUT1 = RA3 (J2-18). v<mV> s t c l p k<mV>");
    delay(20);
    Serial.print("# DC 1650 mV -> read-back on A2: "); Serial.print(readbackMv()); Serial.println(" mV");
}

void loop()
{
    /* waveform generator, paced by micros(): 64 samples per period */
    static unsigned long tNext = 0;
    static uint8_t idx = 0;
    if (mode != DC && (long)(micros() - tNext) >= 0) {
        const unsigned long STEP_US = 1000000UL / (50UL * 64UL);   /* 50 Hz x 64 = 312 us */
        tNext += STEP_US;
        idx = (uint8_t)((idx + 1) & 63);
        if (mode == SINE) dacWrite((uint16_t)sineTab[idx]);
        else if (mode == TRI) dacWrite((uint16_t)(idx < 32 ? idx * 128 : (63 - idx) * 128));
        else dacWrite((uint16_t)((idx / 4) * 273));                 /* 16 steps of ~220 mV */
    }

    /* comparator: LED = "pot is above the DAC threshold" */
    digitalWrite(LED_BUILTIN, DAC1CONLbits.CMPSTAT ? LED_ON : LED_OFF);
    static unsigned long lastEdges = 0;
    if (cmpEdges != lastEdges) {
        lastEdges = cmpEdges;
        Serial.print("# comparator edge #"); Serial.print(cmpEdges);
        Serial.println(DAC1CONLbits.CMPSTAT ? "  pot ABOVE threshold" : "  pot below threshold");
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'v') { dcMv = readNumber(); mode = DC; dacWriteMv(dcMv);
                        delay(5); Serial.print("# DC "); Serial.print(dcMv); Serial.print(" mV, read-back ");
                        Serial.print(readbackMv()); Serial.println(" mV"); }
        else if (c == 's') { mode = SINE;  tNext = micros(); Serial.println("# sine 50 Hz, 0.1..3.2 V"); }
        else if (c == 't') { mode = TRI;   tNext = micros(); Serial.println("# triangle 50 Hz"); }
        else if (c == 'c') { mode = STAIR; tNext = micros(); Serial.println("# staircase, 16 steps"); }
        else if (c == 'l') { mode = DC; linearity(); dacWriteMv(dcMv); }
        else if (c == 'k') { long mv = readNumber(); mode = DC; dcMv = mv; dacWriteMv(mv);
                             Serial.print("# comparator threshold = "); Serial.print(mv);
                             Serial.println(" mV. Turn the pot on J2-15 through it."); }
        else if (c == 'p') {
            delay(20); while (Serial.available()) Serial.read();
            unsigned long t = micros();
            while (Serial.available() == 0) {
                /* keep the waveform running while streaming */
                if (mode != DC && (long)(micros() - tNext) >= 0) {
                    tNext += 1000000UL / (50UL * 64UL);
                    idx = (uint8_t)((idx + 1) & 63);
                    if (mode == SINE) dacWrite((uint16_t)sineTab[idx]);
                    else if (mode == TRI) dacWrite((uint16_t)(idx < 32 ? idx * 128 : (63 - idx) * 128));
                    else dacWrite((uint16_t)((idx / 4) * 273));
                }
                if (micros() - t >= 2000) { t = micros(); Serial.println(analogRead(READBACK)); }
            }
            while (Serial.available()) Serial.read();
        }
    }
}
