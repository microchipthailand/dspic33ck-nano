/*
 * Lab 14 - DMA: all 4 channels working at the same time
 * Board: dsPIC33CK Nano   (works at 4 MIPS and 100 MIPS; 100 MIPS recommended)
 *
 * Goal:  the dsPIC33CK has 4 DMA channels. Each one is a tiny "copy engine"
 *        that moves one word or byte every time its trigger fires, while the
 *        CPU keeps running loop(). This lab gives every channel a different
 *        kind of job:
 *
 *   ch | trigger               | from -> to                 | job
 *   ---+-----------------------+----------------------------+---------------------------
 *    0 | ADC AN9 done (1 kHz)  | ADCBUF9   -> adcBuf[256]   | peripheral -> RAM: sampling,
 *      |                       |                            |   ping-pong (half/full IRQ)
 *    1 | software (CHREQ)      | srcBlk[]  -> dstBlk[]      | RAM -> RAM block copy ('m')
 *    2 | SCCP3 PWM period      | wave[512] -> CCP3RB        | RAM -> peripheral: the LED on
 *      |  (256 Hz)             |                            |   D7 "breathes" with 0 CPU
 *    3 | UART1 TX buffer empty | text      -> U1TXREG       | RAM -> peripheral: the status
 *      |                       |                            |   lines are PRINTED by the DMA
 *
 *   Channel 0 has the highest fixed priority (the ADC data is never late),
 *   channel 3 the lowest (a text can wait a few cycles).
 *   All the register work is in the DMA.h tab; this file only says what moves.
 *
 * Wiring (breadboard):
 *   Pot wiper -> A2 = RA2 (J2-17), pot ends to 3V3 (J3-1) and GND (J3-2)
 *   LED + 330R: D7 = RB2 (J3-17) -> 330R -> LED anode, LED cathode -> GND (J3-2)
 *   Optional scope on D6 = RB1 (J3-20): the 1 kHz sample clock of channel 0
 *   Optional: jumper D5 = RB0 (J3-21) -> J2-17 and send 'p' to feed a 490 Hz
 *             PWM square wave into the ADC (shows ALIASING, see below)
 *
 * Serial commands (115200):
 *   w  dump the last 128 raw samples (open Serial Plotter to see the waveform)
 *   m  channel 1 copy race: DMA vs CPU loop vs memcpy(), and how much the CPU
 *      can do WHILE the DMA copies
 *   b  next LED waveform (breathe / heartbeat / ramp) - the CPU only rewrites
 *      the table in RAM, channel 2 picks it up on its next pass
 *   p  toggle the 490 Hz test PWM on D5
 *   s  status of all 4 channels now
 *
 * Register facts (MCC device data for DSPIC33CK256MC005):
 *   DMAINTx.CHSEL 0x31 ADC AN9 done, 0x12 SCCP3 OC/IC event, 0x05 UART1 TX
 *   ADTRIGx TRGSRC 0x15 = "SCCP2 OC/IC Event", 0x0D = "SCCP2 Trigger Event"
 * NOT hardware-tested yet. Three trigger paths were not verified, so setup()
 * checks each one and falls back if it does not move data:
 *   ADC start   : SCCP2 event 0x15 -> 0x0D -> a tiny SCCP2 ISR starts the ADC
 *   LED channel : SCCP3 event      -> a tiny SCCP3 ISR kicks channel 2
 *   Serial      : UART1 TX event   -> the CPU prints the text instead
 * The start banner says which path each channel uses.
 * Note: the MPLAB X v6.35 simulator cannot test this lab. Its DMA model reads
 * the source into DMABUF but never writes it (DBUFWF stays 1), so in the
 * simulator every channel reports NONE and the CPU prints the text.
 *
 * Aliasing: a 490 Hz square wave sampled at 1000 Hz is above Nyquist (500 Hz).
 * The samples alternate high/low but drift slowly: you see a fake ~20 Hz wave
 * (|1000 - 2*490| = 20 Hz). That is why real systems put a low-pass filter in
 * front of the ADC.
 */

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "DMA.h"

/* ---- which channel does what ---- */
#define CH_ADC   0
#define CH_COPY  1
#define CH_WAVE  2
#define CH_TEXT  3

/* ---- channel 0: ADC sampling ---- */
const unsigned long SAMPLE_HZ = 1000;
#define N_BUF   256                        /* total samples, 2 halves */
#define N_HALF  (N_BUF / 2)
const uint8_t AN_CH        = 9;            /* A2 = RA2 = AN9 */
const uint8_t CLOCK_PIN    = 6;            /* D6 = RB1, SCCP2 = sample clock */
const uint8_t TEST_PWM_PIN = 5;            /* D5 = RB0, SCCP1 */

volatile uint16_t adcBuf[N_BUF];
volatile uint8_t  halfReady = 0;           /* 1 = first half, 2 = second half */
volatile unsigned long halves = 0, overruns = 0;
uint8_t adcPath = 0;                       /* 1: OC event, 2: trigger event, 3: ISR */

/* ---- channel 1: block copy ---- */
#define N_COPY 256
uint16_t srcBlk[N_COPY], dstBlk[N_COPY];
unsigned long copies = 0;

/* ---- channel 2: LED waveform ---- */
const uint8_t  WAVE_PIN    = 7;            /* D7 = RB2, SCCP3 */
const unsigned WAVE_PWM_HZ = 256;          /* 512 steps / 256 Hz = 2 s per cycle */
#define N_WAVE 512
uint16_t wave[N_WAVE];                     /* CCP3RB values, read by the DMA */
uint16_t wavePeriod;
volatile unsigned long waveCycles = 0;
uint8_t wavePath = 0;                      /* 1: SCCP3 event, 2: ISR kick */
uint8_t waveShape = 0;

/* ================= interrupts ================= */
/* ch0 half / full: tell loop() which half is ready */
extern "C" void __attribute__((interrupt, no_auto_psv)) _DMA0Interrupt(void)
{
    uint16_t f = dmaTakeFlags(CH_ADC);
    uint8_t h = 0;
    if (f & DMAINT_HALFIF) h = 1;
    if (f & DMAINT_DONEIF) h = 2;          /* RELOAD: starts again at adcBuf[0] */
    if (h) {
        if (halfReady) overruns++;         /* CPU did not finish the last half */
        halfReady = h;
        halves++;
    }
    _DMA0IF = 0;
}

/* ch2 done = one full pass through wave[] (RELOAD starts the next one) */
extern "C" void __attribute__((interrupt, no_auto_psv)) _DMA2Interrupt(void)
{
    if (dmaTakeFlags(CH_WAVE) & DMAINT_DONEIF) waveCycles++;
    _DMA2IF = 0;
}

/* Fallback only (adcPath 3): SCCP2 period starts one conversion */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT2Interrupt(void)
{
    ADCON3Lbits.CNVCHSEL = AN_CH;
    ADCON3Lbits.CNVRTCH = 1;
    _CCT2IF = 0;
}

/* Fallback only (wavePath 2): SCCP3 period kicks one channel-2 transfer */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT3Interrupt(void)
{
    dmaKick(CH_WAVE);
    _CCT3IF = 0;
}

/* ================= helpers ================= */
/* Let analogWrite() set up PMD/PPS/PWM mode, then change the period. */
uint16_t pwmRetime(uint8_t pin, unsigned long hz)
{
    static const uint8_t PS[4] = { 1, 4, 16, 64 };
    analogWrite(pin, 128);
    unsigned long ps = PS[(pin == CLOCK_PIN) ? CCP2CON1Lbits.TMRPS : CCP3CON1Lbits.TMRPS];
    uint16_t period = (uint16_t)(FCY / (ps * hz) - 1UL);
    if (pin == CLOCK_PIN) {
        CCP2CON1Lbits.CCPON = 0; CCP2TMRL = 0; CCP2PRL = period;
        CCP2RA = 0; CCP2RB = period / 2; CCP2CON1Lbits.CCPON = 1;
    } else {
        CCP3CON1Lbits.CCPON = 0; CCP3TMRL = 0; CCP3PRL = period;
        CCP3RA = 0; CCP3RB = period / 2; CCP3CON1Lbits.CCPON = 1;
    }
    return period;
}

bool moves(uint8_t ch, unsigned ms)        /* does DMACNT change on its own? */
{
    uint16_t c0 = dmaLeft(ch);
    delay(ms);
    return dmaLeft(ch) != c0;
}

/* ================= channel 0: ADC -> RAM ================= */
void startAdcChannel(void)
{
    analogRead(A2);                        /* core powers the ADC, RA2 -> analog */
    ADIELbits.IE9 = 1;                     /* AN9 "done" event (for the DMA) ...  */
    IEC6bits.ADCAN9IE = 0;                 /* ... but NO CPU interrupt            */

    dmaSetup(CH_ADC, &ADCBUF9, adcBuf, N_BUF, DMA_TRG_ADC_AN(AN_CH),
             DMA_REPEAT_ONE_SHOT, DMA_FIXED, DMA_INC, DMA_16BIT, DMA_RELOAD);
    dmaHalfInterrupt(CH_ADC, true);        /* interrupt at half AND at full */
    dmaInterrupt(CH_ADC, 5);
    dmaStart(CH_ADC);

    pwmRetime(CLOCK_PIN, SAMPLE_HZ);       /* SCCP2 = 1 kHz sample clock on D6 */
    ADTRIG2Lbits.TRGSRC9 = 0x15;           /* SCCP2 OC/IC event */
    if (moves(CH_ADC, 30)) { adcPath = 1; return; }
    ADTRIG2Lbits.TRGSRC9 = 0x0D;           /* SCCP2 trigger event */
    if (moves(CH_ADC, 30)) { adcPath = 2; return; }
    ADTRIG2Lbits.TRGSRC9 = 0x00;           /* fallback: ISR starts each conversion */
    _CCT2IP = 4; _CCT2IF = 0; _CCT2IE = 1;
    adcPath = moves(CH_ADC, 30) ? 3 : 0;
}

/* ================= channel 2: RAM table -> PWM duty ================= */
void fillWave(uint8_t shape)
{
    for (int i = 0; i < N_WAVE; i++) {
        float t = (float)i / N_WAVE, y;    /* t = 0..1 over one cycle */
        if (shape == 0) {                  /* breathe: smooth sine, squared for the eye */
            y = (1.0f - cosf(2.0f * (float)M_PI * t)) * 0.5f;
            y = y * y;
        } else if (shape == 1) {           /* heartbeat: two quick beats, then rest */
            float a = (t - 0.10f) / 0.035f, b = (t - 0.28f) / 0.05f;
            y = expf(-a * a) + 0.6f * expf(-b * b);
        } else {                           /* ramp up, then off */
            y = t * t;
        }
        if (y > 1.0f) y = 1.0f;
        /* keep 1..period-1: a duty of 0 or 100 % has no compare event */
        wave[i] = 1 + (uint16_t)(y * (wavePeriod - 2));
    }
}

void startWaveChannel(void)
{
    wavePeriod = pwmRetime(WAVE_PIN, WAVE_PWM_HZ);
    fillWave(waveShape);
    dmaSetup(CH_WAVE, wave, &CCP3RB, N_WAVE, DMA_TRG_SCCP3,
             DMA_REPEAT_ONE_SHOT, DMA_INC, DMA_FIXED, DMA_16BIT, DMA_RELOAD);
    dmaInterrupt(CH_WAVE, 2);              /* count passes (DONE) */
    dmaStart(CH_WAVE);
    if (moves(CH_WAVE, 30)) { wavePath = 1; return; }
    _CCT3IP = 3; _CCT3IF = 0; _CCT3IE = 1; /* fallback: kick from the period ISR */
    wavePath = moves(CH_WAVE, 30) ? 2 : 0;
}

/* ================= channel 1: copy race ================= */
void copyRace(void)
{
    const int REPS = 200;
    for (int i = 0; i < N_COPY; i++) srcBlk[i] = (uint16_t)(i * 7919u);
    unsigned long t0, tDma, tLoop, tMemcpy;
    bool ok = true;

    memset(dstBlk, 0, sizeof(dstBlk));
    t0 = micros();
    for (int r = 0; r < REPS && ok; r++) {
        dmaCopyStart(CH_COPY, dstBlk, srcBlk, N_COPY);
        ok = dmaCopyWait(CH_COPY);
        copies++;
    }
    tDma = micros() - t0;
    ok = ok && memcmp(dstBlk, srcBlk, sizeof(srcBlk)) == 0;

    t0 = micros();
    for (int r = 0; r < REPS; r++) {
        volatile uint16_t *d = dstBlk;
        for (int i = 0; i < N_COPY; i++) d[i] = srcBlk[i];
    }
    tLoop = micros() - t0;

    t0 = micros();
    for (int r = 0; r < REPS; r++) memcpy(dstBlk, srcBlk, sizeof(srcBlk));
    tMemcpy = micros() - t0;

    /* overlap: how much work the CPU gets done while ONE DMA copy runs */
    memset(dstBlk, 0, sizeof(dstBlk));
    unsigned long work = 0;
    dmaCopyStart(CH_COPY, dstBlk, srcBlk, N_COPY);
    while (!dmaDone(CH_COPY) && work < 1000000UL) work++;   /* stand-in for useful CPU work */
    bool overlapOk = dmaDone(CH_COPY);
    dmaStop(CH_COPY);
    copies++;

    dmaPrintf("# ch1 copy 256 words x%d:\r\n", REPS);
    dmaPrintf("#   DMA ch1  %lu us total %s\r\n", tDma, ok ? "(data OK)" : "(DATA MISMATCH / TIMEOUT!)");
    dmaPrintf("#   CPU loop %lu us total\r\n", tLoop);
    dmaPrintf("#   memcpy   %lu us total\r\n", tMemcpy);
    if (overlapOk) dmaPrintf("#   while ONE DMA copy ran, the CPU counted to %lu\r\n", work);
    else           dmaPrintf("#   overlap test: the DMA copy never finished\r\n");
    dmaPrintf("# DMA is not always faster - its win is that the CPU is FREE while it runs.\r\n");
}

/* ================= reports (sent by channel 3) ================= */
void processHalf(uint8_t h)
{
    const volatile uint16_t *s = adcBuf + (h == 1 ? 0 : N_HALF);
    uint16_t mn = 0xFFFF, mx = 0;
    uint32_t sum = 0;
    for (int i = 0; i < N_HALF; i++) {
        uint16_t v = s[i] & 0x0FFF;        /* 12-bit result */
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    float mean = (float)sum / N_HALF, acc = 0;
    for (int i = 0; i < N_HALF; i++) { float d = (s[i] & 0x0FFF) - mean; acc += d * d; }
    unsigned rms10 = (unsigned)(sqrtf(acc / N_HALF) * 10.0f + 0.5f);
    unsigned mv = (unsigned)(mean * 3300.0f / 4095.0f + 0.5f);

    if ((halves & 7) == 0)                 /* one line per second (8 halves) */
        dmaPrintf("ch0 half %u  mean=%u mV  min=%u max=%u  rms(ac)=%u.%u LSB  overruns=%lu\r\n",
                  h, mv, mn, mx, rms10 / 10, rms10 % 10, overruns);
}

void printStatus(unsigned long idleLoops)
{
    dmaPrintf("# ch0 %lu samples | ch1 %lu copies | ch2 %lu LED cycles | ch3 %lu bytes | idle %lu/5s\r\n",
              halves * N_HALF, copies, waveCycles, dmaTxBytes, idleLoops);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    dmaBegin();
    dmaSerialBegin(CH_TEXT);

    dmaPrint("# Lab 14 - DMA, 4 channels. Pot on A2 (J2-17), LED on D7 (J3-17).\r\n");
    dmaPrint("# commands: w = dump samples, m = copy race, b = LED shape, p = test PWM, s = status\r\n");
    dmaSerialWait();                       /* proves (or disproves) the ch3 path */

    startAdcChannel();
    startWaveChannel();

    static const char *ADC[] = { "NONE - no samples! check the ADC/DMA setup",
                                 "SCCP2 OC event -> ADC -> DMA (0 CPU per sample)",
                                 "SCCP2 trigger event -> ADC -> DMA (0 CPU per sample)",
                                 "timer ISR starts ADC -> DMA moves data (fallback)" };
    static const char *WAV[] = { "NONE - LED channel not moving!",
                                 "SCCP3 event -> DMA -> CCP3RB (0 CPU)",
                                 "SCCP3 ISR kicks DMA (fallback)" };
    dmaPrintf("# ch0 ADC : %s\r\n", ADC[adcPath]);
    dmaPrintf("# ch1 copy: software request (send 'm')\r\n");
    dmaPrintf("# ch2 LED : %s\r\n", WAV[wavePath]);
    dmaPrintf("# ch3 text: %s\r\n", dmaTxWorks ? "UART1 TX event -> DMA -> U1TXREG"
                                                : "UART1 TX event did not work - CPU prints");
}

void loop()
{
    static unsigned long idleLoops = 0;
    idleLoops++;                           /* how free the CPU is */

    if (halfReady) {
        uint8_t h = halfReady;
        processHalf(h);
        halfReady = 0;
        digitalWrite(LED_BUILTIN, h == 1 ? LED_ON : LED_OFF);   /* toggles every 128 ms */
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'w') {
            halfReady = 0;
            unsigned long t0 = millis();
            while (halfReady == 0 && millis() - t0 < 500) { }   /* wait for a fresh half */
            if (halfReady == 0) { dmaPrint("# no samples - channel 0 is not running\r\n"); return; }
            const volatile uint16_t *s = adcBuf + (halfReady == 1 ? 0 : N_HALF);
            for (int i = 0; i < N_HALF; i++) dmaPrintf("%u\r\n", s[i] & 0x0FFF);
            halfReady = 0;
        } else if (c == 'm') {
            copyRace();
        } else if (c == 'b') {
            static const char *NAME[] = { "breathe", "heartbeat", "ramp" };
            waveShape = (waveShape + 1) % 3;
            fillWave(waveShape);           /* channel 2 keeps running */
            dmaPrintf("# ch2 LED shape: %s\r\n", NAME[waveShape]);
        } else if (c == 'p') {
            static bool on = false;
            on = !on;
            analogWrite(TEST_PWM_PIN, on ? 128 : 0);
            dmaPrintf(on ? "# test PWM on D5 ON (jumper J3-21 -> J2-17)\r\n" : "# test PWM OFF\r\n");
        } else if (c == 's') {
            printStatus(idleLoops);
        }
    }

    static unsigned long lastReport = 0;
    if (millis() - lastReport >= 5000) {
        lastReport = millis();
        printStatus(idleLoops);
        idleLoops = 0;
    }
}
