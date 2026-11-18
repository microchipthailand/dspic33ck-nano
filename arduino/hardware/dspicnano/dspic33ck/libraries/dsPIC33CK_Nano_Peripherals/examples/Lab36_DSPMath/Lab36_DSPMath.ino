/*
 * Lab 36 - DSP math: Q15 fixed point and the MAC unit
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (4 MIPS works, 25x slower)
 *
 * Goal:  1. Understand Q15 numbers: fractions stored in an int16_t.
 *        2. Use the DSP engine: 40-bit accumulators, saturation, and the MAC
 *           instruction (multiply + add in one cycle).
 *        3. Measure: the same 64-tap filter in float, in plain C integers
 *           and with the MAC unit.
 *        4. Filter a signal (FIR low-pass) and watch it in the Serial Plotter.
 *        5. Detect tones with the Goertzel algorithm (a telephone keypad).
 *        6. Filter a real signal from the ADC in real time.
 *
 * Hardware: none for 1-5 (the signals are generated in software).
 *   For 6: a potentiometer on A0 = RA0 (J2-15), or any 0-3.3 V signal.
 *
 * Serial commands (Serial Monitor on J5, 115200, line ending "Newline"):
 *   q        Q15 basics: multiply, overflow vs saturation
 *   b        benchmark: 64-tap FIR over 256 samples, float / int32 / MAC / REPEAT+MAC
 *   f        FIR demo: 200 Hz + 2500 Hz + noise -> 800 Hz low-pass
 *            (open Tools > Serial Plotter, then send f)
 *   r        RMS of a test sine with the MAC unit
 *   g<key>   Goertzel: make the DTMF tone of a key and decode it, e.g. g5, g#
 *   a        live: A0 sampled at 1 kHz, low-pass filtered, to the plotter (a again = stop)
 *
 * ----------------------------------------------------------------------------
 * WHY FIXED POINT
 *   This chip has no floating-point unit: a float multiply is a library call
 *   of ~30-100 instructions. A Q15 multiply-accumulate is ONE instruction. For
 *   signal processing (filters, control loops, motor control) that is the
 *   difference between "real time" and "too slow".
 * WHY 40-BIT ACCUMULATORS
 *   A 64-tap filter adds 64 products. Even if the final answer fits in Q15,
 *   the running sum may not. The 8 guard bits hold up to 256x full scale;
 *   only the final store (__builtin_sacr) saturates to +-1.
 * WHY REPEAT + PREFETCH
 *   The MAC builtin in a C loop is no faster than plain C: the loads and the
 *   loop counter cost ~10 cycles. "repeat" + "mac ... [w8]+=2, w4, [w10]+=2, w6"
 *   fetches the next operands while it multiplies: 1 cycle per tap.
 * The DSP.h tab has the functions and a short reference of the builtins.
 * Verified in the MPLAB simulator (all but 'a', the simulator has no ADC).
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <NanoTimer.h>
#include "DSP.h"

const unsigned long BAUD = 115200;
const float FS = 8000.0f;                   /* sample rate of the generated signals */

/* ---------------- 1. Q15 basics ---------------- */
void printQ15(const char *label, q15 v)
{
    Serial.print("  "); Serial.print(label); Serial.print(" = 0x");
    Serial.print((uint16_t)v, HEX); Serial.print(" = "); Serial.println(q15ToFloat(v), 5);
}

void q15Basics()
{
    Serial.println("# Q15: value = int16 / 32768");
    printQ15("Q15(0.5)          ", Q15(0.5));
    printQ15("Q15(-0.25)        ", Q15(-0.25));
    printQ15("0.5 * 0.5         ", q15mul(Q15(0.5), Q15(0.5)));
    printQ15("0.75 * -0.5       ", q15mul(Q15(0.75), Q15(-0.5)));
    printQ15("-1 * -1 (!)       ", q15mul(Q15(-1.0), Q15(-1.0)));
    Serial.println("    (+1 does not exist in Q15: the plain C result wraps to -1)");
    printQ15("0.9 + 0.9  plain C", q15addWrap(Q15(0.9), Q15(0.9)));
    dspSaturation(true);
    printQ15("0.9 + 0.9  SATA on", q15addSat(Q15(0.9), Q15(0.9)));
    dspSaturation(false);
    printQ15("0.9 + 0.9  SATA off, saturating store", q15addSat(Q15(0.9), Q15(0.9)));
    Serial.println("    (with SATA off the 40-bit accumulator holds 1.8; the store saturates it)");
    Serial.print("  CORCON = 0x"); Serial.println(CORCON, HEX);
}

/* ---------------- test signals ---------------- */
q15 sig[256];

/* white noise -1 .. +1: a 32-bit linear congruential generator */
uint32_t noiseState = 1;
float noise1()
{
    noiseState = noiseState * 1664525UL + 1013904223UL;
    return (int16_t)(noiseState >> 16) / 32768.0f;
}

void makeTone(q15 *x, uint16_t n, float f1, float a1, float f2, float a2, float noise)
{
    for (uint16_t i = 0; i < n; i++) {
        float v = a1 * sinf(2 * (float)M_PI * f1 * i / FS) + a2 * sinf(2 * (float)M_PI * f2 * i / FS);
        if (noise > 0) v += noise * noise1();
        x[i] = floatToQ15(v);
    }
}

/* ---------------- 2. benchmark ---------------- */
const uint16_t NT = 64;
q15 hq[NT];
q15 xq[2 * NT] DSP_YMEM;                    /* delay line in Y memory: fast MAC */
float hf[NT], xf[NT];

void benchmark()
{
    firDesignLowpass(hq, NT - 1, 800, FS);  /* 63 taps + one zero tap */
    hq[NT - 1] = 0;
    for (uint16_t i = 0; i < NT; i++) hf[i] = q15ToFloat(hq[i]);
    makeTone(sig, 256, 200, 0.4f, 2500, 0.4f, 0.1f);

    unsigned long t0, tF, tC, tB, tM;
    volatile float fo = 0; volatile q15 co = 0, bo = 0, mo = 0;

    /* float */
    for (uint16_t i = 0; i < NT; i++) xf[i] = 0;
    t0 = micros();
    for (uint16_t n = 0; n < 256; n++) {
        for (uint16_t k = NT - 1; k > 0; k--) xf[k] = xf[k - 1];
        xf[0] = q15ToFloat(sig[n]);
        fo = fDot(hf, xf, NT);
    }
    tF = micros() - t0;

    /* plain C, int32 */
    Fir f; firInit(f, hq, xq, NT);
    t0 = micros();
    for (uint16_t n = 0; n < 256; n++) {
        f.pos = (f.pos == 0) ? NT - 1 : f.pos - 1;
        f.x[f.pos] = sig[n]; f.x[f.pos + NT] = sig[n];
        co = cDot(hq, &f.x[f.pos], NT);
    }
    tC = micros() - t0;

    /* MAC builtin in a C loop */
    firInit(f, hq, xq, NT);
    t0 = micros();
    for (uint16_t n = 0; n < 256; n++) {
        f.pos = (f.pos == 0) ? NT - 1 : f.pos - 1;
        f.x[f.pos] = sig[n]; f.x[f.pos + NT] = sig[n];
        bo = dspDot(hq, &f.x[f.pos], NT);
    }
    tB = micros() - t0;

    /* REPEAT + MAC with prefetch (firRun -> dspDotFast) */
    firInit(f, hq, xq, NT);
    t0 = micros();
    for (uint16_t n = 0; n < 256; n++) mo = firRun(f, sig[n]);
    tM = micros() - t0;

    const float macs = 256.0f * NT;
    Serial.println("# 64-tap FIR, 256 samples (16384 multiply-adds):");
    Serial.print("  float          "); Serial.print(tF); Serial.print(" us  = ");
    Serial.print(tF * (FCY / 1e6f) / macs, 1); Serial.print(" cycles/tap   last out "); Serial.println(fo, 4);
    Serial.print("  int32 plain C  "); Serial.print(tC); Serial.print(" us  = ");
    Serial.print(tC * (FCY / 1e6f) / macs, 1); Serial.print(" cycles/tap   last out "); Serial.println(q15ToFloat(co), 4);
    Serial.print("  MAC in C loop  "); Serial.print(tB); Serial.print(" us  = ");
    Serial.print(tB * (FCY / 1e6f) / macs, 1); Serial.print(" cycles/tap   last out "); Serial.println(q15ToFloat(bo), 4);
    Serial.print("  REPEAT+MAC     "); Serial.print(tM); Serial.print(" us  = ");
    Serial.print(tM * (FCY / 1e6f) / macs, 1); Serial.print(" cycles/tap   last out "); Serial.println(q15ToFloat(mo), 4);
    Serial.print("  REPEAT+MAC is "); Serial.print((float)tF / tM, 1); Serial.print("x faster than float, ");
    Serial.print((float)tC / tM, 1); Serial.println("x faster than int32 C");
    Serial.println("  (the cycles/tap include the per-sample overhead: call, delay-line update)");
    Serial.print("  -> a 64-tap filter could run at up to "); Serial.print(256.0f * 1e6f / tM / 1000.0f, 0);
    Serial.println(" k samples/s on this clock");
}

/* ---------------- 3. FIR demo for the plotter ---------------- */
const uint16_t NF = 63;
q15 hLp[NF];
q15 dLp[2 * NF] DSP_YMEM;

void firDemo()
{
    firDesignLowpass(hLp, NF, 800, FS);
    Fir f; firInit(f, hLp, dLp, NF);
    makeTone(sig, 256, 200, 0.4f, 2500, 0.4f, 0.1f);
    for (uint16_t n = 0; n < 256; n++) {
        q15 y = firRun(f, sig[n]);
        Serial.print("input:"); Serial.print(q15ToFloat(sig[n]), 3);
        Serial.print(",lowpass_800Hz:"); Serial.println(q15ToFloat(y), 3);
    }
    float inRms = q15ToFloat(dspRms(sig, 256));
    Serial.print("# input RMS "); Serial.print(inRms, 3);
    Serial.println("  (the output is delayed by (63-1)/2 = 31 samples: the filter's group delay)");
}

/* ---------------- 4. RMS ---------------- */
void rmsDemo()
{
    makeTone(sig, 256, 250, 0.5f, 0, 0, 0);     /* 250 Hz: exactly 8 periods in 256 */
    unsigned long t0 = micros();
    q15 r = dspRms(sig, 256);
    unsigned long t = micros() - t0;
    Serial.print("# sine amplitude 0.5: RMS by MAC = "); Serial.print(q15ToFloat(r), 4);
    Serial.print("  (theory 0.5/sqrt(2) = 0.3536) in "); Serial.print(t); Serial.println(" us");
}

/* ---------------- 5. Goertzel DTMF ---------------- */
const float ROW[4] = { 697, 770, 852, 941 };
const float COL[4] = { 1209, 1336, 1477, 1633 };
const char KEYS[4][5] = { "123A", "456B", "789C", "*0#D" };

void dtmf(char key)
{
    int r = -1, c = -1;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (KEYS[i][j] == key) { r = i; c = j; }
    if (r < 0) { Serial.println("# key must be 0-9 * # A-D"); return; }
    const uint16_t N = 205;                 /* the classic DTMF block: 205 samples at 8 kHz */
    makeTone(sig, N, ROW[r], 0.4f, COL[c], 0.4f, 0.2f);
    Serial.print("# tone for key '"); Serial.print(key); Serial.print("' = ");
    Serial.print(ROW[r], 0); Serial.print(" + "); Serial.print(COL[c], 0); Serial.println(" Hz, with noise");
    unsigned long t0 = micros();
    float pr[4], pc[4]; int br = 0, bc = 0;
    for (int i = 0; i < 4; i++) {
        pr[i] = goertzelPower(sig, N, ROW[i], FS); if (pr[i] > pr[br]) br = i;
        pc[i] = goertzelPower(sig, N, COL[i], FS); if (pc[i] > pc[bc]) bc = i;
    }
    unsigned long t = micros() - t0;
    Serial.print("  rows   "); for (int i = 0; i < 4; i++) { Serial.print(ROW[i], 0); Serial.print(":"); Serial.print(pr[i], 3); Serial.print("  "); } Serial.println();
    Serial.print("  cols   "); for (int i = 0; i < 4; i++) { Serial.print(COL[i], 0); Serial.print(":"); Serial.print(pc[i], 3); Serial.print("  "); } Serial.println();
    Serial.print("  decoded key: '"); Serial.print(KEYS[br][bc]); Serial.print("'  (8 Goertzel bins in ");
    Serial.print(t); Serial.println(" us)");
}

/* ---------------- 6. live ADC filter ---------------- */
const uint16_t NA = 31;
q15 hLive[NA];
q15 dLive[2 * NA] DSP_YMEM;
Fir live;
volatile q15 liveIn = 0, liveOut = 0;
volatile bool liveNew = false;
bool liveOn = false;

void liveSample()                           /* 1 kHz, from Timer1 (NanoTimer) */
{
    q15 x = (q15)((analogRead(A0) - 512) << 6);     /* analogRead() = 10 bit -> Q15, centred */
    liveIn = x;
    liveOut = firRun(live, x);
    liveNew = true;
}

void liveToggle()
{
    liveOn = !liveOn;
    if (liveOn) {
        firDesignLowpass(hLive, NA, 10, 1000);      /* 10 Hz low-pass at fs = 1 kHz */
        firInit(live, hLive, dLive, NA);
        Timer1.begin(1000, liveSample);
    } else Timer1.stop();
}

unsigned long liveCount = 0;

void help()
{
    Serial.println();
    Serial.println("Lab 36 - DSP math   q Q15  b benchmark  f FIR->plotter  r RMS  g<key> DTMF  a live ADC");
}

void setup()
{
    Serial.begin(BAUD);
    help();
}

char line[16];
uint8_t lineLen = 0;

void command(const char *cmd)
{
    /* The live filter runs in an interrupt and uses accumulator A, like the
     * other demos. Two users of one accumulator at once would corrupt each
     * other, so stop the live filter first. */
    if (liveOn && cmd[0] != 'a') { liveToggle(); Serial.println("# live filter stopped"); }
    switch (cmd[0]) {
    case 'q': q15Basics(); break;
    case 'b': benchmark(); break;
    case 'f': firDemo(); break;
    case 'r': rmsDemo(); break;
    case 'g': dtmf(cmd[1] ? cmd[1] : '5'); break;
    case 'a': liveToggle(); break;
    default: help(); break;
    }
}

void loop()
{
    if (liveOn && liveNew) {
        liveNew = false;
        if (++liveCount % 10 == 0) {        /* plot every 10th sample: 100 lines/s */
            Serial.print("A0:"); Serial.print(q15ToFloat(liveIn), 3);
            Serial.print(",filtered:"); Serial.println(q15ToFloat(liveOut), 3);
        }
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
