/*
 * Lab 14 - DMA: ADC sampling with zero CPU load + memory-to-memory copy
 * Board: dsPIC33CK Nano   (works at 4 MIPS and 100 MIPS; 100 MIPS recommended)
 *
 * Goal:  let the hardware do the work. A timer (SCCP2) triggers the ADC at an
 *        exact 1 kHz, and DMA channel 0 moves every result into a RAM buffer
 *        WITHOUT the CPU executing a single instruction per sample. The CPU is
 *        only told "half the buffer is full" (ping-pong / double buffering).
 *        Then DMA channel 1 copies a RAM block and we race it against the CPU.
 *
 * Wiring (breadboard):
 *   Pot wiper -> A2 = RA2 (J2-17), pot ends to 3V3 (J3-1) and GND (J3-2)
 *   Optional scope / logic analyser on D6 = RB1 (J3-20): the 1 kHz sample clock
 *   Optional: jumper D5 = RB0 (J3-21) -> J2-17 and send 'p' to feed a 490 Hz
 *             PWM square wave into the ADC (shows ALIASING - see below)
 *
 * Data path (no CPU involved):
 *
 *   SCCP2 timer --(OC event, 1 kHz)--> ADC trigger AN9 --(conversion done)-->
 *   DMA ch0 trigger --> copy ADCBUF9 to adcBuf[i++] --> half/full interrupt
 *
 *   adcBuf[256] = two halves of 128 samples. While the CPU processes half A,
 *   the DMA fills half B, and vice versa. 128 samples at 1 kHz = 128 ms to
 *   finish processing before the data is overwritten.
 *
 * Register facts (from the MCC device data for DSPIC33CK256MC005, not memory):
 *   DMAINTx.CHSEL 0x31 = "ADC AN9 Conversion Complete Event"
 *   ADTRIGx TRGSRC 0x15 = "SCCP2 OC/IC Event", 0x0D = "SCCP2 Trigger Event"
 *   DMACHx.TRMODE 0 one-shot, 1 repeated one-shot, 2 continuous, 3 repeated continuous
 *   DMACHx.SAMODE/DAMODE 0 unchanged, 1 increment, 2 decrement
 * NOT hardware-tested yet. Because the exact SCCP event that reaches the ADC in
 * PWM mode was not verified, setup() tries 0x15, then 0x0D, and finally falls
 * back to a tiny timer ISR that starts each conversion (DMA still moves the
 * data). It prints which one worked.
 *
 * Serial commands (115200):
 *   w  dump the last 128 raw samples (open Serial Plotter to see the waveform)
 *   m  run the memory-copy race: DMA ch1 vs CPU loop vs memcpy()
 *   p  toggle the 490 Hz test PWM on D5
 *
 * Aliasing: a 490 Hz square wave sampled at 1000 Hz is above Nyquist (500 Hz).
 * The samples alternate high/low but drift slowly: you see a fake ~20 Hz wave
 * (|1000 - 2*490| = 20 Hz). That is why real systems put a low-pass filter in
 * front of the ADC.
 */

#include <Arduino.h>
#include <string.h>

const unsigned long SAMPLE_HZ = 1000;
#define N_BUF   256                        /* total samples, 2 halves */
#define N_HALF  (N_BUF / 2)
const uint8_t AN_CH        = 9;            /* A2 = RA2 = AN9 (shared ADC core) */
const uint8_t CLOCK_PIN    = 6;            /* D6 = RB1, PWM2 = SCCP2 */
const uint8_t TEST_PWM_PIN = 5;            /* D5 = RB0, PWM1 = SCCP1 */

volatile uint16_t adcBuf[N_BUF];
volatile uint8_t  halfReady = 0;           /* 1 = first half, 2 = second half */
volatile unsigned long halves = 0, overruns = 0;
uint8_t triggerMode = 0;                   /* 1: OC event, 2: trigger event, 3: ISR */

/* ================= DMA channel 0 interrupt: half / full ================= */
extern "C" void __attribute__((interrupt, no_auto_psv)) _DMA0Interrupt(void)
{
    uint8_t h = 0;
    if (DMAINT0bits.HALFIF) { DMAINT0bits.HALFIF = 0; h = 1; }
    if (DMAINT0bits.DONEIF) { DMAINT0bits.DONEIF = 0; h = 2; }
    if (DMAINT0bits.OVRUNIF) DMAINT0bits.OVRUNIF = 0;   /* trigger came while busy */
    if (h) {
        if (halfReady) overruns++;         /* CPU did not finish the last half */
        halfReady = h;
        halves++;
    }
    IFS0bits.DMA0IF = 0;
}

/* Fallback only (triggerMode 3): SCCP2 period interrupt starts one conversion */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT2Interrupt(void)
{
    ADCON3Lbits.CNVCHSEL = AN_CH;
    ADCON3Lbits.CNVRTCH = 1;
    IFS1bits.CCT2IF = 0;
}

/* ---------------- SCCP2 = sample clock, visible on D6 ---------------- */
void startSampleClock(unsigned long hz)
{
    analogWrite(CLOCK_PIN, 128);           /* core does PMD, PPS and PWM mode */
    static const uint8_t PS[4] = { 1, 4, 16, 64 };
    unsigned long period = FCY / ((unsigned long)PS[CCP2CON1Lbits.TMRPS] * hz) - 1UL;
    CCP2CON1Lbits.CCPON = 0;
    CCP2TMRL = 0;
    CCP2PRL  = (uint16_t)period;
    CCP2RA   = 0;
    CCP2RB   = (uint16_t)(period / 2);     /* 50 % duty square wave */
    CCP2CON1Lbits.CCPON = 1;
}

/* ---------------- ADC: AN9 converted on a hardware trigger ---------------- */
void adcSetTrigger(uint8_t trgsrc)
{
    ADTRIG2Lbits.TRGSRC9 = trgsrc;         /* TRGSRC8 and TRGSRC9 live in ADTRIG2L */
}

/* ---------------- DMA channel 0: ADCBUF9 -> adcBuf[] ---------------- */
void dmaStartAdc(void)
{
    PMD6bits.DMA0MD = 0;
    PMD6bits.DMA1MD = 0;
    DMACONbits.DMAEN = 1;
    DMAL = 0x1000;                         /* RAM window the DMA may write (MCC default) */
    DMAH = 0x8FFF;

    DMACH0 = 0;                            /* channel off while configuring */
    DMAINT0 = 0;
    DMASRC0 = (uint16_t)(&ADCBUF9);        /* fixed source: the AN9 result register */
    DMADST0 = (uint16_t)adcBuf;            /* destination walks through the buffer */
    DMACNT0 = N_BUF;
    DMACH0bits.SIZE   = 0;                 /* 16-bit words */
    DMACH0bits.SAMODE = 0;                 /* source unchanged */
    DMACH0bits.DAMODE = 1;                 /* destination incremented */
    DMACH0bits.TRMODE = 1;                 /* repeated one-shot: 1 word per trigger */
    DMACH0bits.RELOAD = 1;                 /* reload SRC/DST/CNT at the end -> endless */
    DMAINT0bits.CHSEL = 0x31;              /* trigger = ADC AN9 conversion complete */
    DMAINT0bits.HALFEN = 1;                /* interrupt at half AND at full */

    IPC1bits.DMA0IP = 5;
    IFS0bits.DMA0IF = 0;
    IEC0bits.DMA0IE = 1;
    DMACH0bits.CHEN = 1;
}

bool samplesArriving(void)
{
    uint16_t c0 = DMACNT0;
    delay(30);                             /* 30 samples' worth at 1 kHz */
    return DMACNT0 != c0 || halves > 0;
}

void startAcquisition(void)
{
    analogRead(A2);                        /* core powers the ADC, RA2 -> analog */
    ADIELbits.IE9 = 1;                     /* AN9 "done" event (for the DMA) ...  */
    IEC6bits.ADCAN9IE = 0;                 /* ... but NO CPU interrupt            */
    dmaStartAdc();
    startSampleClock(SAMPLE_HZ);

    adcSetTrigger(0x15);                   /* SCCP2 OC/IC event */
    if (samplesArriving()) { triggerMode = 1; return; }
    adcSetTrigger(0x0D);                   /* SCCP2 trigger event */
    if (samplesArriving()) { triggerMode = 2; return; }

    adcSetTrigger(0x00);                   /* fallback: ISR starts each conversion */
    IPC6bits.CCT2IP = 4;
    IFS1bits.CCT2IF = 0;
    IEC1bits.CCT2IE = 1;
    triggerMode = samplesArriving() ? 3 : 0;
}

/* ---------------- memory-to-memory race (DMA channel 1) ---------------- */
#define N_COPY 256
uint16_t srcBlk[N_COPY], dstBlk[N_COPY];

void dmaCopy(uint16_t *dst, const uint16_t *src, uint16_t words)
{
    DMACH1 = 0;
    DMAINT1 = 0;
    DMASRC1 = (uint16_t)src;
    DMADST1 = (uint16_t)dst;
    DMACNT1 = words;
    DMACH1bits.SAMODE = 1;                 /* both pointers increment */
    DMACH1bits.DAMODE = 1;
    DMACH1bits.TRMODE = 2;                 /* continuous: whole block on one request */
    DMAINT1bits.CHSEL = 0x18;              /* CRC event - unused, so never fires */
    DMACH1bits.CHEN = 1;
    DMACH1bits.CHREQ = 1;                  /* software request = GO */
    while (!DMAINT1bits.DONEIF) { }        /* the CPU could do other work here */
    DMACH1bits.CHEN = 0;
}

void copyRace(void)
{
    const int REPS = 200;
    for (int i = 0; i < N_COPY; i++) srcBlk[i] = (uint16_t)(i * 7919u);
    unsigned long t0, tDma, tLoop, tMemcpy;

    memset(dstBlk, 0, sizeof(dstBlk));
    t0 = micros();
    for (int r = 0; r < REPS; r++) dmaCopy(dstBlk, srcBlk, N_COPY);
    tDma = micros() - t0;
    bool okDma = memcmp(dstBlk, srcBlk, sizeof(srcBlk)) == 0;

    memset(dstBlk, 0, sizeof(dstBlk));
    t0 = micros();
    for (int r = 0; r < REPS; r++) {
        volatile uint16_t *d = dstBlk;
        for (int i = 0; i < N_COPY; i++) d[i] = srcBlk[i];
    }
    tLoop = micros() - t0;

    t0 = micros();
    for (int r = 0; r < REPS; r++) memcpy(dstBlk, srcBlk, sizeof(srcBlk));
    tMemcpy = micros() - t0;

    Serial.print("# copy 256 words x"); Serial.print(REPS); Serial.println(":");
    Serial.print("#   DMA ch1  "); Serial.print(tDma);    Serial.print(" us total ");
    Serial.println(okDma ? "(data OK)" : "(DATA MISMATCH!)");
    Serial.print("#   CPU loop "); Serial.print(tLoop);   Serial.println(" us total");
    Serial.print("#   memcpy   "); Serial.print(tMemcpy); Serial.println(" us total");
    Serial.println("# DMA is not always faster - its win is that the CPU is FREE while it runs.");
}

/* ---------------- statistics on one finished half ---------------- */
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
    double mean = (double)sum / N_HALF;
    double acc = 0;
    for (int i = 0; i < N_HALF; i++) { double d = (s[i] & 0x0FFF) - mean; acc += d * d; }
    double rmsNoise = sqrt(acc / N_HALF);

    Serial.print("half "); Serial.print(h);
    Serial.print("  mean="); Serial.print(mean * 3.3 / 4095.0, 3);
    Serial.print(" V  min="); Serial.print(mn);
    Serial.print(" max="); Serial.print(mx);
    Serial.print("  rms(ac)="); Serial.print(rmsNoise, 1);
    Serial.print(" LSB  halves="); Serial.print(halves);
    Serial.print(" overruns="); Serial.println(overruns);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("# Lab 14 - DMA. Pot on A2 (J2-17). Sample clock on D6 (J3-20).");
    Serial.println("# commands: w = dump samples, m = copy race, p = 490 Hz test PWM on D5");

    startAcquisition();
    static const char *MODE[] = { "NONE - no samples! check the ADC/DMA setup",
                                  "SCCP2 OC event -> ADC -> DMA (0 CPU per sample)",
                                  "SCCP2 trigger event -> ADC -> DMA (0 CPU per sample)",
                                  "timer ISR starts ADC -> DMA moves data (fallback)" };
    Serial.print("# trigger path: ");
    Serial.println(MODE[triggerMode]);
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
            while (halfReady == 0) { }     /* wait for a fresh half */
            const volatile uint16_t *s = adcBuf + (halfReady == 1 ? 0 : N_HALF);
            for (int i = 0; i < N_HALF; i++) Serial.println((int)(s[i] & 0x0FFF));
            halfReady = 0;
        } else if (c == 'm') {
            copyRace();
        } else if (c == 'p') {
            static bool on = false;
            on = !on;
            if (on) analogWrite(TEST_PWM_PIN, 128); else analogWrite(TEST_PWM_PIN, 0);
            Serial.println(on ? "# test PWM on D5 ON (jumper J3-21 -> J2-17)" : "# test PWM OFF");
        }
    }

    static unsigned long lastReport = 0;
    if (millis() - lastReport >= 5000) {
        lastReport = millis();
        Serial.print("# CPU idle loop iterations in 5 s: ");
        Serial.println(idleLoops);
        idleLoops = 0;
    }
}
