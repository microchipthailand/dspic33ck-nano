/*
 * Lab 31 - Analog comparator: a fast "above / below" detector
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required - the DAC needs the PLL)
 *
 * Goal:  1. Compare a voltage with a threshold set by the internal 12-bit DAC.
 *        2. See why hysteresis matters: count the edges of a slowly moving,
 *           noisy input with 0, 15, 30 and 45 mV of hysteresis.
 *        3. Route the comparator output straight to a pin with PPS - no CPU
 *           in the path at all.
 *        4. Count edges in the interrupt: a frequency counter.
 *        5. Build an ADC out of the DAC and the comparator (successive
 *           approximation) and compare it with analogRead().
 *
 * Hardware:
 *   10k potentiometer: ends to 3V3 (J3-1) and GND (J3-2), wiper to A0 = RA0
 *   (J2-15) = comparator input A. (The same pot as Lab 04.)
 *   Frequency test: a jumper from D8 = RB3 (J3-16) to D7 = RB2 (J3-17) =
 *   comparator input D. 'g' puts a test tone on D8.
 *   Optional: scope / LED + 1k on D9 = RB4 (J3-15): the comparator output.
 *
 * Serial commands (Serial Monitor on J5, 115200, line ending "Newline"):
 *   ?          status
 *   v<mV>      threshold, e.g.  v1200
 *   a / d      input A (pot)  /  input D (D7, for the frequency test)
 *   h<0-3>     hysteresis 0 / 15 / 30 / 45 mV
 *   p          invert the output on / off
 *   f          digital filter on / off
 *   o          comparator -> USER LED directly (PPS) on / off
 *   c          count edges for 5 s: turn the pot slowly through the threshold
 *   g          test tone 1 kHz on D8 on / off (jumper D8 -> D7, then 'd')
 *   n          frequency: rising edges in 1 s
 *   m          measure the input with DAC + comparator (SAR) vs analogRead()
 *   s          stream for the Serial Plotter on / off
 *
 * ----------------------------------------------------------------------------
 * THRESHOLD   Vth = 3.3 V * DAC1DATH / 4096   (0.8 mV steps)
 * HYSTERESIS  without it, a slow or noisy input near Vth makes the output
 *             chatter: many edges for one crossing. With hysteresis the
 *             comparator needs the input to move H mV back before it flips
 *             back, so noise smaller than H is ignored.
 * PPS         comparator output = PPS output code 23. Here it always drives D9
 *             (RB4 = RP36); 'o' also puts it on the USER LED (RC13 = RP61).
 *             The LED is active low: it lights while the output is LOW.
 * SAR ADC     try bit 11 (2048): input above? keep it, else drop it; then bit
 *             10 ... down to bit 0. 12 tries = 12 bits.
 * Details and registers: the Comparator.h tab. See also Lab 15 (the DAC).
 * NOT VERIFIED on hardware. The MPLAB simulator models the registers only.
 * ----------------------------------------------------------------------------
 */

#if FCY < 50000000UL
#error "Lab31 needs Tools > Clock = 100 MIPS (the comparator's DAC is clocked from the PLL)"
#endif

#include <Arduino.h>
#include "Comparator.h"

const unsigned long BAUD = 115200;
const uint8_t RP_D9 = 36, RP_LED = 61;
const uint8_t TONE_PIN = 8;

bool streaming = false, toneOn = false, ledDirect = false;
uint8_t hyst = 2;

void status()
{
    Serial.print("# input "); Serial.print(CMP_PIN_NAME[cmpInput]);
    Serial.print("  threshold "); Serial.print(cmpThresholdGetMv()); Serial.print(" mV (code ");
    Serial.print(cmpThresholdCode); Serial.print(")  hysteresis "); Serial.print(CMP_HYST_MV[DAC1CONLbits.HYSSEL]);
    Serial.print(" mV  invert "); Serial.print(DAC1CONLbits.CMPPOL);
    Serial.print("  filter "); Serial.print(DAC1CONLbits.FLTREN);
    Serial.print("  -> output "); Serial.print(cmpOut());
    Serial.print("  edges so far "); Serial.println(cmpEdges);
}

void countEdges()
{
    Serial.println("# counting output changes for 5 s - turn the pot SLOWLY through the threshold once");
    noInterrupts(); unsigned long e0 = cmpEdges; interrupts();
    delay(5000);
    noInterrupts(); unsigned long e = cmpEdges - e0; interrupts();
    Serial.print("# "); Serial.print(e); Serial.print(" edges with "); Serial.print(CMP_HYST_MV[DAC1CONLbits.HYSSEL]);
    Serial.print(" mV hysteresis");
    Serial.println(e > 2 ? "  (more than one per crossing = chatter: try more hysteresis or 'f')" : "");
}

void frequency()
{
    noInterrupts(); unsigned long r0 = cmpRises; interrupts();
    delay(1000);
    noInterrupts(); unsigned long r = cmpRises - r0; interrupts();
    Serial.print("# "); Serial.print(r); Serial.print(" Hz on input "); Serial.println(CMP_PIN_NAME[cmpInput]);
}

void measure()
{
    unsigned long t0 = micros();
    uint16_t sar = cmpMeasureMv();
    unsigned long tSar = micros() - t0;
    uint8_t pin = CMP_PIN[cmpInput];
    uint16_t adc = (uint16_t)((uint32_t)analogRead(pin) * AVDD_MV / 1024UL);   /* analogRead() = 10 bit */
    cmpSelectInput(cmpInput);               /* analogRead may have touched the pin setup */
    Serial.print("# DAC+comparator SAR: "); Serial.print(sar); Serial.print(" mV (");
    Serial.print(tSar); Serial.print(" us)   analogRead: "); Serial.print(adc); Serial.println(" mV");
}

void help()
{
    Serial.println();
    Serial.println("Lab 31 - analog comparator CMP1 + DAC threshold");
    Serial.println("  ? v<mV> a d h<0-3> p f o c g n m s   (see the sketch header)");
}

void setup()
{
    Serial.begin(BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(9, OUTPUT);
    cmpBegin(CMP_IN_A);
    cmpThresholdMv(1650);
    cmpHysteresis(hyst);
    cmpOutputToPin(RP_D9);                  /* hardware output on D9, always */
    help();
    status();
}

char line[16];
uint8_t lineLen = 0;
unsigned long lastStream = 0;
bool lastOut = false;

void command(const char *cmd)
{
    switch (cmd[0]) {
    case '?': status(); break;
    case 'v': cmpThresholdMv(atoi(cmd + 1)); status(); break;
    case 'a': cmpSelectInput(CMP_IN_A); status(); break;
    case 'd': cmpSelectInput(CMP_IN_D); status(); break;
    case 'h': hyst = (uint8_t)atoi(cmd + 1) & 3; cmpHysteresis(hyst); status(); break;
    case 'p': cmpInvert(!DAC1CONLbits.CMPPOL); status(); break;
    case 'f': cmpFilter(!DAC1CONLbits.FLTREN); status(); break;
    case 'o':
        ledDirect = !ledDirect;
        cmpOutputToPin(RP_LED, ledDirect);
        Serial.println(ledDirect ? "# USER LED = comparator output, by hardware (lit = output LOW)"
                                 : "# USER LED back to software (lit = output HIGH)");
        break;
    case 'c': countEdges(); break;
    case 'g':
        toneOn = !toneOn;
        if (toneOn) tone(TONE_PIN, 1000); else noTone(TONE_PIN);
        Serial.println(toneOn ? "# 1 kHz on D8 (J3-16). Jumper it to D7 (J3-17), select input 'd', then 'n'"
                              : "# tone off");
        break;
    case 'n': frequency(); break;
    case 'm': measure(); break;
    case 's': streaming = !streaming; break;
    default: break;
    }
}

void loop()
{
    bool out = cmpOut();
    if (!ledDirect) digitalWrite(LED_BUILTIN, out ? LED_ON : LED_OFF);
    if (out != lastOut && !streaming) {
        lastOut = out;
        Serial.println(out ? "# input ABOVE threshold" : "# input below threshold");
    }

    if (streaming && millis() - lastStream >= 50) {     /* Serial Plotter, 20 Hz */
        lastStream = millis();
        uint16_t mv = (uint16_t)((uint32_t)analogRead(CMP_PIN[cmpInput]) * AVDD_MV / 1024UL);
        cmpSelectInput(cmpInput);
        Serial.print("input:"); Serial.print(mv);
        Serial.print(",threshold:"); Serial.print(cmpThresholdGetMv());
        Serial.print(",output:"); Serial.println(out ? 3000 : 0);
    }

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n' || lineLen >= sizeof(line) - 1) {
            line[lineLen] = 0;
            if (lineLen) command(line);
            lineLen = 0;
        } else line[lineLen++] = c;
    }
}
