/*
 * Lab 38 - Advanced ADC: every mode of the 12-bit ADC
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (4 MIPS works, slower)
 *
 * Goal:  analogRead() gives 10 bits from one pin at a time. The ADC of this
 *        chip can do much more. With the functions in the ADC.h tab:
 *        1. Read ALL 22 channels (20 pins + temperature + band gap) at full
 *           12 bits, all from ONE trigger.
 *        2. Change the resolution (6 / 8 / 10 / 12 bit) and the sample time,
 *           and measure the conversion speed.
 *        3. Sample at an exact rate with a hardware trigger (timer, PWM),
 *           with an interrupt per result - no CPU timing involved.
 *        4. Convert continuously at full speed (level trigger).
 *        5. Let the digital comparator watch a window without the CPU.
 *        6. Get 13-16 bits by oversampling, or smooth by averaging, in hardware.
 *        7. Try the other output formats: signed, differential, fractional.
 *        8. Measure the chip's own supply voltage with the band gap, and its
 *           temperature sensor.
 *
 * Hardware:
 *   10k potentiometer: ends to 3V3 (J3-1) and GND (J3-2), wiper to A0 = RA0
 *   (J2-15) = channel AN0. Optional: a second pot (or any 0-3.3 V signal) on
 *   other analog pins (table in ADC.h), and for the differential test two 10k
 *   resistors from 3V3 and GND to RD13 = ANN0 (J2-13), i.e. 1.65 V there.
 *
 * Serial commands (Serial Monitor on J5, 115200, line ending "Newline"):
 *   ?          help + settings
 *   a          ALL channels: one common trigger, table of all 22 results
 *   r<ch>      one channel, 16 readings + statistics      r0   r17
 *   b          resolution: AN0 at 6, 8, 10 and 12 bits
 *   s / s<tad> speed test / set the sample time in TAD     s   s50
 *   t<hz>      TIMER-triggered sampling of AN0, 1 s        t1000  t50000
 *   p<hz>      PWM-triggered sampling of AN0, 0.5 s        p20000
 *   l          continuous conversion (level trigger), 100 ms
 *   c<lo> <hi> digital comparator: LED + message when AN0 leaves lo..hi mV
 *   c          comparator off
 *   o<n>       oversampling filter, n = 4 / 16 / 64 / 256 (13..16 bit)
 *   v<n>       averaging filter,    n = 2 .. 256
 *   d          differential + signed: AN0 - ANN0 (RD13)
 *   f          fractional (left-aligned) result format
 *   i          internal channels: band gap -> AVDD, temperature sensor
 *   g          AN0..AN4 to the Serial Plotter (g again = stop)
 *
 * NOT VERIFIED: the MPLAB simulator has no working ADC model, and the board
 * has not been built yet. Where the data sheet leaves a doubt (which timer
 * trigger code works, whether the common interrupt fires), the sketch tries
 * the possibilities and prints which one it used.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "ADC.h"

const unsigned long BAUD = 115200;
const uint8_t CH = 0;                       /* AN0 = RA0 = A0 = the pot (J2-15) */

/* ---------------- statistics of a run of samples ---------------- */
volatile unsigned long nSamples = 0;
volatile uint32_t sumS = 0;
volatile uint64_t sumSq = 0;
volatile uint16_t minS = 0xFFFF, maxS = 0;

void statsReset() { noInterrupts(); nSamples = 0; sumS = 0; sumSq = 0; minS = 0xFFFF; maxS = 0; interrupts(); }
void onSample(uint8_t, uint16_t v)
{
    nSamples++; sumS += v; sumSq += (uint32_t)v * v;
    if (v < minS) minS = v;
    if (v > maxS) maxS = v;
}

void printStats(const char *what, unsigned long ms)
{
    noInterrupts();
    unsigned long n = nSamples; uint32_t s = sumS; uint64_t q = sumSq; uint16_t lo = minS, hi = maxS;
    interrupts();
    Serial.print("# "); Serial.print(what); Serial.print(": "); Serial.print(n); Serial.print(" samples in ");
    Serial.print(ms); Serial.print(" ms = "); Serial.print(n * 1000.0 / ms, 0); Serial.println(" per second");
    if (!n) return;
    double mean = (double)s / n;
    double var = (double)q / n - mean * mean;
    Serial.print("  mean "); Serial.print(mean, 1); Serial.print(" (= "); Serial.print(adcToMv((int32_t)(mean + 0.5)));
    Serial.print(" mV)  min "); Serial.print(lo); Serial.print("  max "); Serial.print(hi);
    Serial.print("  noise (std dev) "); Serial.print(var > 0 ? sqrt(var) : 0.0, 2); Serial.println(" LSB");
}

/* ---------------- 1. all channels ---------------- */
void allChannels()
{
    uint16_t v[ADC_CHANNELS];
    unsigned long t0 = micros();
    bool ok = adcScan(ADC_ALL, v);
    unsigned long t = micros() - t0;
    Serial.print("# all 22 channels from ONE common software trigger, "); Serial.print(adcBits);
    Serial.print(" bit, "); Serial.print(t); Serial.println(ok ? " us" : " us  (TIME-OUT on some channels)");
    Serial.println("  ch  pin         raw     mV   ");
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        Serial.print(ch < 10 ? "  AN" : "  AN"); Serial.print(ch); Serial.print(ch < 10 ? "   " : "  ");
        Serial.print(ADC_PIN_NAME[ch]); Serial.print("  ");
        if (v[ch] < 1000) Serial.print(' '); if (v[ch] < 100) Serial.print(' '); if (v[ch] < 10) Serial.print(' ');
        Serial.print(v[ch]); Serial.print("  ");
        int32_t mv = adcToMv(v[ch]);
        if (mv < 1000) Serial.print(' '); if (mv < 100) Serial.print(' '); if (mv < 10) Serial.print(' ');
        Serial.print(mv); Serial.print("  ");
        for (uint8_t k = 0; k < (uint8_t)(mv / 110); k++) Serial.print('#');     /* bar: 30 = 3.3 V */
        Serial.println();
    }
    Serial.println("  (unconnected pins float: their values are random)");
}

/* ---------------- 2. one channel ---------------- */
void readChannel(uint8_t ch)
{
    if (ch >= ADC_CHANNELS) { Serial.println("# channel 0..21"); return; }
    statsReset();
    for (uint8_t i = 0; i < 16; i++) onSample(ch, (uint16_t)adcRead(ch));
    Serial.print("# AN"); Serial.print(ch); Serial.print(" ("); Serial.print(ADC_PIN_NAME[ch]); Serial.print("), ");
    Serial.print(adcBits); Serial.println(" bit, 16 single conversions");
    printStats("readings", 1);
    if (adcTimedOut) Serial.println("  TIME-OUT: no conversion finished (MPLAB simulator: expected)");
}

/* ---------------- 3. resolution and speed ---------------- */
float speedTest(uint8_t ch)
{
    const uint16_t N = 1000;
    unsigned long t0 = micros();
    for (uint16_t i = 0; i < N; i++) adcRead(ch);
    unsigned long t = micros() - t0;
    return (float)t / N;                    /* us per conversion, including the software */
}

void resolutionDemo()
{
    Serial.println("# AN0 at each resolution (same voltage):");
    Serial.println("  bits   max code   code    mV    1 LSB      time/conversion");
    const uint8_t B[4] = { 6, 8, 10, 12 };
    for (uint8_t i = 0; i < 4; i++) {
        adcResolution(B[i]);
        int16_t raw = adcRead(CH);
        float us = speedTest(CH);
        Serial.print("  "); Serial.print(B[i]); Serial.print(B[i] < 10 ? "     " : "    ");
        Serial.print(adcMaxCode()); Serial.print("\t   "); Serial.print(raw); Serial.print("\t");
        Serial.print(adcToMv(raw)); Serial.print("\t"); Serial.print((float)adcAvddMv / (1U << B[i]), 3);
        Serial.print(" mV   "); Serial.print(us, 2); Serial.println(" us");
    }
    adcResolution(12);
    Serial.println("  fewer bits = coarser steps but a shorter conversion");
}

uint16_t sampleTad = 17;
void speed()
{
    float us = speedTest(CH);
    Serial.print("# sample time "); Serial.print(sampleTad); Serial.print(" TAD, "); Serial.print(adcBits);
    Serial.print(" bit: "); Serial.print(us, 2); Serial.print(" us per adcRead() = ");
    Serial.print(1000.0 / us, 0); Serial.println(" k conversions/s (software-started, one by one)");
}

/* ---------------- 4. hardware-triggered sampling ---------------- */
int8_t timerPath = -1;                      /* 0 = SCCP3 code 22, 1 = SCCP3 Trigger code 14, 2 = software */
const char *const PATH_NAME[3] = { "SCCP3 (TRGSRC 22)", "SCCP3 Trigger (TRGSRC 14)", "SCCP3 interrupt + software trigger" };

void startTimerPath(int8_t path, unsigned long hz)
{
    adcSoftTriggerCh = 0xFF;
    if (path == 0) adcTrigger(CH, ADC_TRG_SCCP3);
    else if (path == 1) adcTrigger(CH, ADC_TRG_SCCP3_TRIG);
    else { adcTrigger(CH, ADC_TRG_NONE); adcSoftTriggerCh = CH; }
    adcTimerStart(hz);
}
void stopSampling()
{
    adcTimerStop(); adcPwmTriggerStop();
    adcSoftTriggerCh = 0xFF;
    adcTrigger(CH, ADC_TRG_NONE);
}

/* collect for `ms`, delivering results from the interrupt or, if it does not
 * fire, from adcPoll() */
void collect(unsigned long ms)
{
    unsigned long t0 = millis();
    while (millis() - t0 < ms) adcPoll();
}

void findTimerPath()
{
    adcAttachInterrupt(CH, onSample);
    for (int8_t p = 0; p < 3; p++) {
        statsReset();
        startTimerPath(p, 1000);
        collect(30);
        stopSampling();
        if (nSamples > 5) { timerPath = p; break; }
    }
    adcDetachInterrupt(CH);
    Serial.print("# timer trigger path: "); Serial.println(timerPath >= 0 ? PATH_NAME[timerPath] : "NONE works");
}

void timedSampling(unsigned long hz)
{
    if (hz < 10 || hz > 200000) { Serial.println("# rate 10 .. 200000 Hz"); return; }
    if (timerPath < 0) findTimerPath();
    if (timerPath < 0) return;
    adcAttachInterrupt(CH, onSample);
    unsigned long irq0 = adcIrqCount;
    statsReset();
    startTimerPath(timerPath, hz);
    collect(1000);
    stopSampling();
    adcDetachInterrupt(CH);
    Serial.print("# TIMER-triggered AN0 at "); Serial.print(hz); Serial.print(" Hz via "); Serial.println(PATH_NAME[timerPath]);
    printStats("timer", 1000);
    Serial.println(adcIrqCount != irq0 ? "  results delivered by the ADC interrupt (_ADCInterrupt)"
                                       : "  the ADC interrupt did not fire: results were polled with adcPoll()");
}

void pwmSampling(unsigned long hz)
{
    if (hz < 1600 || hz > 200000) { Serial.println("# rate 1600 .. 200000 Hz"); return; }
    adcAttachInterrupt(CH, onSample);
    statsReset();
    adcTrigger(CH, ADC_TRG_PWM1_1);
    adcPwmTriggerStart(hz, 0.5f);           /* sample in the middle of each PWM period */
    collect(500);
    stopSampling();
    adcDetachInterrupt(CH);
    Serial.print("# PWM-triggered AN0: PG1 at "); Serial.print(hz); Serial.println(" Hz, TRIGA at 50 % of the period");
    printStats("PWM", 500);
    if (!nSamples) Serial.println("  no samples: check PG1 (Lab 16/37) and ADTRIG0L = 4");
}

void continuousDemo()
{
    adcAttachInterrupt(CH, onSample);
    statsReset();
    adcTrigger(CH, ADC_TRG_SW_LEVEL);
    adcContinuous(true);                    /* convert again and again */
    collect(100);
    adcContinuous(false);
    adcTrigger(CH, ADC_TRG_NONE);
    adcDetachInterrupt(CH);
    printStats("CONTINUOUS (level trigger)", 100);
}

/* ---------------- 5. digital comparator ---------------- */
bool cmpOn = false;
unsigned long cmpShown = 0, ledUntil = 0;

void comparator(long loMv, long hiMv, bool on)
{
    adcCompare(0, CH, 0, 0, 0);
    stopSampling();
    cmpOn = on;
    if (!on) { Serial.println("# comparator off"); return; }
    if (timerPath < 0) findTimerPath();
    uint16_t lo = (uint16_t)((uint32_t)loMv * (1UL << adcBits) / adcAvddMv);
    uint16_t hi = (uint16_t)((uint32_t)hiMv * (1UL << adcBits) / adcAvddMv);
    adcCompare(0, CH, lo, hi, ADC_CMP_OUTSIDE);
    startTimerPath(timerPath < 0 ? 2 : timerPath, 1000);   /* AN0 converts 1000 times a second */
    cmpShown = adcCmpEvents[0];
    Serial.print("# comparator 0 watches AN0: event when it leaves "); Serial.print(loMv); Serial.print(" .. ");
    Serial.print(hiMv); Serial.print(" mV (codes "); Serial.print(lo); Serial.print(" .. "); Serial.print(hi);
    Serial.println("). The CPU does not look at the results.");
}

/* ---------------- 6. filters ---------------- */
double stdDevOf(uint32_t n, double s, double q) { double m = s / n, v = q / n - m * m; return v > 0 ? sqrt(v) : 0; }

void filterDemo(bool oversample, uint16_t ratio)
{
    if (oversample && ratio != 4 && ratio != 16 && ratio != 64 && ratio != 256) { Serial.println("# o4 o16 o64 o256"); return; }
    if (!oversample && (ratio < 2 || ratio > 256 || (ratio & (ratio - 1)))) { Serial.println("# v2 v4 ... v256"); return; }
    /* plain 12-bit noise first */
    double s = 0, q = 0;
    for (uint8_t i = 0; i < 32; i++) { double v = (uint16_t)adcRead(CH); s += v; q += v * v; }
    double plainSd = stdDevOf(32, s, q), plainMean = s / 32;

    uint8_t bits = oversample ? adcOversampleBits(ratio) : 12;
    double scale = (double)(1UL << (bits - 12));                 /* filter LSBs per 12-bit LSB */
    adcFilter(0, CH, oversample ? ADC_FLT_OVERSAMPLE : ADC_FLT_AVERAGE, ratio);
    adcTrigger(CH, ADC_TRG_SW_LEVEL);
    adcContinuous(true);
    s = q = 0; uint8_t n = 0; unsigned long t0 = millis();
    while (n < 32 && millis() - t0 < 1000) {
        (void)adcResult(CH);                 /* keep the channel's own buffer drained */
        if (adcFilterReady(0)) { double v = adcFilterResult(0); s += v; q += v * v; n++; }
    }
    adcContinuous(false);
    adcTrigger(CH, ADC_TRG_NONE);
    adcFilterOff(0);
    Serial.print("# "); Serial.print(oversample ? "OVERSAMPLING " : "AVERAGING "); Serial.print(ratio);
    Serial.print("x on AN0 -> "); Serial.print(bits); Serial.println("-bit results");
    Serial.print("  plain 12 bit:   mean "); Serial.print(plainMean, 2); Serial.print("  noise "); Serial.print(plainSd, 2); Serial.println(" LSB(12)");
    if (!n) { Serial.println("  no filter results (filter RDY never set)"); return; }
    Serial.print("  filter output:  mean "); Serial.print(s / n, 1); Serial.print(" = "); Serial.print(s / n / scale, 3);
    Serial.print(" in 12-bit units  noise "); Serial.print(stdDevOf(n, s, q) / scale, 3); Serial.println(" LSB(12)");
    Serial.print("  = "); Serial.print(s / n / scale * adcAvddMv / 4096.0, 2); Serial.println(" mV");
}

/* ---------------- 7. formats ---------------- */
void differentialDemo()
{
    adcMode(CH, true, true);
    Serial.println("# AN0 - ANN0 (RD13, J2-13), signed: 0 = equal, + = AN0 higher");
    for (uint8_t i = 0; i < 8; i++) {
        int16_t v = adcRead(CH);
        Serial.print("  "); Serial.print(v); Serial.print("  ("); Serial.print((long)v * adcAvddMv / 2048); Serial.println(" mV difference)");
        delay(100);
    }
    adcMode(CH, false, false);
}

void fractionalDemo()
{
    uint16_t intV = (uint16_t)adcRead(CH);
    adcFractional(true);
    uint16_t frV = (uint16_t)adcRead(CH);
    adcFractional(false);
    Serial.print("# AN0 integer: 0x"); Serial.print(intV, HEX); Serial.print(" = "); Serial.print(intV);
    Serial.print("   fractional: 0x"); Serial.print(frV, HEX); Serial.print(" = "); Serial.print(frV / 65536.0, 4);
    Serial.println(" of full scale (12 bits left-aligned in 16)");
}

/* ---------------- 8. internal channels ---------------- */
void internalDemo()
{
    uint16_t bg = (uint16_t)adcRead(ADC_CH_BANDGAP);
    uint16_t vdd = adcMeasureVdd();
    Serial.print("# band gap AN21: raw "); Serial.print(bg); Serial.print(" -> AVDD = 1200 mV x 4096 / raw = ");
    Serial.print(vdd); Serial.println(" mV (all mV values now use this)");
    Serial.print("# temperature sensor AN20: "); Serial.print(adcTemperatureMv());
    Serial.println(" mV (falls as the chip warms; calibrate two points to get degC - see the lab)");
}

/* ---------------- 9. plotter ---------------- */
bool plotOn = false;
unsigned long lastPlot = 0;
void plot()
{
    uint16_t v[ADC_CHANNELS];
    adcScan(0x1F, v);                       /* AN0..AN4 at the same moment */
    for (uint8_t ch = 0; ch < 5; ch++) {
        Serial.print("AN"); Serial.print(ch); Serial.print(':'); Serial.print(adcToMv(v[ch]));
        Serial.print(ch < 4 ? ',' : '\n');
    }
}

/* ---------------- commands ---------------- */
void help()
{
    Serial.println();
    Serial.println("Lab 38 - advanced ADC (ADC.h)   a all channels  r<ch>  b bits  s speed  t<hz> timer  p<hz> PWM  l continuous");
    Serial.println("  c<lo> <hi> comparator  o<n> oversample  v<n> average  d differential  f fractional  i internal  g plot  ? help");
    Serial.print("# "); Serial.print(adcBits); Serial.print(" bit, sample time "); Serial.print(sampleTad);
    Serial.print(" TAD, AVDD "); Serial.print(adcAvddMv); Serial.println(" mV");
}

void command(char *cmd)
{
    char op = cmd[0];
    char *rest = cmd + 1;
    long a = strtol(rest, &rest, 10), b = strtol(rest, 0, 10);
    bool hasNum = cmd[1] != 0;
    if (op != 'g') plotOn = false;
    switch (op) {
    case '?': help(); break;
    case 'a': allChannels(); break;
    case 'r': readChannel(hasNum ? (uint8_t)a : CH); break;
    case 'b': resolutionDemo(); break;
    case 's': if (hasNum) { sampleTad = (uint16_t)a; adcSampleTime(sampleTad); } speed(); break;
    case 't': timedSampling(hasNum ? a : 1000); break;
    case 'p': pwmSampling(hasNum ? a : 20000); break;
    case 'l': continuousDemo(); break;
    case 'c': comparator(a, b, hasNum && b > a); break;
    case 'o': filterDemo(true, hasNum ? (uint16_t)a : 16); break;
    case 'v': filterDemo(false, hasNum ? (uint16_t)a : 16); break;
    case 'd': differentialDemo(); break;
    case 'f': fractionalDemo(); break;
    case 'i': internalDemo(); break;
    case 'g': plotOn = !plotOn; break;
    default: help(); break;
    }
}

void setup()
{
    Serial.begin(BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    adcBegin();
    adcSampleTime(sampleTad);
    adcMeasureVdd();
    help();
    allChannels();
}

char line[32];
uint8_t lineLen = 0;

void loop()
{
    if (cmpOn) {
        if (adcCmpEvents[0] != cmpShown) {
            cmpShown = adcCmpEvents[0];
            ledUntil = millis() + 100;
            static unsigned long lastMsg = 0;
            if (millis() - lastMsg > 300) {
                lastMsg = millis();
                Serial.print("# comparator: AN0 outside the window (events "); Serial.print(cmpShown); Serial.println(")");
            }
        }
        (void)adcResult(CH);                /* keep AN0's buffer drained */
    }
    digitalWrite(LED_BUILTIN, millis() < ledUntil ? LED_ON : LED_OFF);
    if (plotOn && millis() - lastPlot >= 20) { lastPlot = millis(); plot(); }

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
