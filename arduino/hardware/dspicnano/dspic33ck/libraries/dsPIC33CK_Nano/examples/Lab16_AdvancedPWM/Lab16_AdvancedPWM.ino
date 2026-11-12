/*
 * Lab 16 - Advanced PWM: motor-control PWM generators, dead time, 3-phase sine
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required)
 *
 * Goal:  go beyond analogWrite(). This chip ("MC" = Motor Control) has 4
 *        dedicated PWM generators PG1-PG4. Each one drives a PAIR of pins
 *        (H and L) that are complementary with programmable DEAD TIME, all
 *        generators can be synchronised, and the hardware updates duty cycles
 *        glitch-free. That is exactly what a 3-phase inverter / BLDC drive needs.
 *
 * What it does:
 *   PG1, PG2, PG3 = 20 kHz centre-aligned complementary PWM, 500 ns dead time
 *   An interrupt at the PWM rate steps through a sine table -> the three duty
 *   cycles form a 3-phase sine (120 deg apart), modulation frequency 1..100 Hz.
 *   Low-pass filter one output (RC) and you see the sine on a scope / ADC.
 *
 * Pins (fixed PWM pins - no PPS):
 *   PG1  PWM1H = RB14 (J2-8)    PWM1L = RB15 (J2-9)     phase U
 *   PG2  PWM2H = RB12 (J2-6)    PWM2L = RB13 (J2-7)     phase V
 *   PG3  PWM3H = RB10 (J2-4)    PWM3L = RB11 (J2-5)     phase W
 *   (RB10-RB12 are also Serial2 / RS485 in Labs 21 and 24 - not used here.)
 *
 * Wiring (to SEE it):
 *   - Logic analyser / scope on J2-8 and J2-9: complementary, with a gap
 *     (dead time) where BOTH are low.
 *   - RC filter: J2-8 -> 10k -> node, node -> 100 nF -> ground,
 *     node -> A0 = RA0 (J2-15).
 *     Serial Plotter shows the filtered sine of phase U (command 'a').
 *   - LEDs (with 1k) on J2-8/J2-6/J2-4 at 1 Hz modulation: three LEDs pulsing
 *     120 deg apart, like a rotating field.
 *   NEVER connect these pins to a real power stage without a gate driver.
 *
 * Serial commands (115200):
 *   f<hz>  modulation frequency 1..100      e.g. f50
 *   m<pc>  modulation depth 0..100 %        e.g. m80
 *   d<ns>  dead time 0..2000 ns             e.g. d1000
 *   s      stop all outputs (override to LOW)    g  go again
 *   a      stream filtered phase U (A0) for the Serial Plotter, any key stops
 *   ?      status, including the MEASURED PWM frequency
 *
 * HOW IT WORKS
 *   PWM clock: PCLKCON.MCLKSEL = 00 selects FCY (the MCC device data lists
 *   FCY / FPLLO / FVCO-2 / FVCO-3 for this part). CLKSEL = 01 = master clock.
 *   Centre-aligned mode counts up AND down, so
 *       f_pwm = f_clk / (2 * PER)     PER = 100 MHz / (2 * 20 kHz) = 2500
 *   NOT hardware-verified: whether the generator really counts at FCY. setup()
 *   therefore MEASURES the PWM frequency with SCCP2 (input capture on RB14,
 *   routed by PPS) and prints it; '?' repeats the measurement.
 *   Dead time: DTH/DTL in clock ticks; 500 ns = 50 ticks at 100 MHz.
 *   Glitch-free updates: new DC values are written to the buffer registers and
 *   PGxSTAT.UPDREQ asks the hardware to load them at the next period start.
 *   PG1 is the master: PG2/PG3 start on PG1's start-of-cycle (SOCS = 1),
 *   so all three are phase-locked.
 *   Safety: PGxIOCONL.OVRENH/OVRENL force both pins LOW ('s' command) -
 *   the same mechanism a fault input would use.
 */

#include <Arduino.h>
#include <math.h>

#if FCY < 50000000UL
#error "Lab16 needs Tools > Clock = 100 MIPS"
#endif

const unsigned long PWM_HZ   = 20000;
const unsigned long PCLK_HZ  = FCY;            /* PWM master clock = FCY (MCLKSEL 00) */
const uint16_t      PER      = (uint16_t)(PCLK_HZ / (2UL * PWM_HZ));   /* centre-aligned */

#define SINE_N 256
int16_t sineTab[SINE_N];                       /* -32767..32767 */

volatile uint32_t phaseAcc = 0;                /* 32-bit phase accumulator (DDS) */
volatile uint32_t phaseInc = 0;
volatile uint16_t depthQ15 = 26214;            /* 80 % */
volatile unsigned long pwmIsrCount = 0;

/* ---------- PGx register access (PG1..PG3 have identical layouts) ---------- */
struct PG {
    volatile uint16_t *CONL, *CONH, *STAT, *IOCONL, *IOCONH, *EVTL, *EVTH, *DC, *PER, *PHASE, *DTL, *DTH;
};
const PG pg[3] = {
    { &PG1CONL, &PG1CONH, &PG1STAT, &PG1IOCONL, &PG1IOCONH, &PG1EVTL, &PG1EVTH, &PG1DC, &PG1PER, &PG1PHASE, &PG1DTL, &PG1DTH },
    { &PG2CONL, &PG2CONH, &PG2STAT, &PG2IOCONL, &PG2IOCONH, &PG2EVTL, &PG2EVTH, &PG2DC, &PG2PER, &PG2PHASE, &PG2DTL, &PG2DTH },
    { &PG3CONL, &PG3CONH, &PG3STAT, &PG3IOCONL, &PG3IOCONH, &PG3EVTL, &PG3EVTH, &PG3DC, &PG3PER, &PG3PHASE, &PG3DTL, &PG3DTH },
};

#define CONL_ON        0x8000u
#define CONL_CLK_MCLK  (1u << 3)        /* CLKSEL = 01: master clock            */
#define CONL_CENTER    0x0004u          /* MODSEL = 100: centre-aligned         */
#define CONH_SOC_PG1   0x0001u          /* SOCS = 0001: start with PG1          */
#define CONH_UPD_SOC   0x0000u          /* UPDMOD = 000: update at start of cycle */
#define IOCONH_PENH    0x0008u
#define IOCONH_PENL    0x0004u          /* PMOD = 00: complementary             */
#define IOCONL_OVRENH  0x2000u
#define IOCONL_OVRENL  0x1000u          /* OVRDAT = 00 -> both forced LOW       */
#define STAT_UPDREQ    0x0008u
#define EVTH_IEVT_EOC  0x0000u          /* IEVTSEL = 00: interrupt at end of cycle */

void setDeadTime(uint16_t ns)
{
    uint16_t ticks = (uint16_t)((uint32_t)ns * (PCLK_HZ / 1000000UL) / 1000UL);
    if (ticks > 0x3FFF) ticks = 0x3FFF;
    for (int i = 0; i < 3; i++) { *pg[i].DTH = ticks; *pg[i].DTL = ticks; *pg[i].STAT |= STAT_UPDREQ; }
}

void outputs(bool on)
{
    for (int i = 0; i < 3; i++) {
        if (on) *pg[i].IOCONL &= (uint16_t)~(IOCONL_OVRENH | IOCONL_OVRENL);
        else    *pg[i].IOCONL |= (IOCONL_OVRENH | IOCONL_OVRENL);
    }
}

void pwmBegin(void)
{
    PMD1bits.PWMMD = 0;                         /* power the PWM module */
    PCLKCON = 0x0000;                           /* MCLKSEL = FCY, DIVSEL n/a */

    for (int i = 0; i < 3; i++) {
        *pg[i].CONL   = 0;                      /* off while configuring */
        *pg[i].CONL   = CONL_CLK_MCLK | CONL_CENTER;
        *pg[i].CONH   = (i == 0 ? 0x0000u : CONH_SOC_PG1) | CONH_UPD_SOC;
        *pg[i].PER    = PER;
        *pg[i].DC     = PER / 2;                /* 50 % = sine zero */
        *pg[i].PHASE  = 0;
        *pg[i].IOCONL = IOCONL_OVRENH | IOCONL_OVRENL;   /* start forced LOW */
        *pg[i].IOCONH = IOCONH_PENH | IOCONH_PENL;       /* PWM owns both pins, complementary */
        *pg[i].EVTL   = 0;
        *pg[i].EVTH   = (i == 0) ? EVTH_IEVT_EOC : 0x0300u;   /* only PG1 interrupts */
    }
    setDeadTime(500);

    IPC16bits.PWM1IP = 5;
    IFS4bits.PWM1IF = 0;
    IEC4bits.PWM1IE = 1;

    /* clients first, master last, so they are already waiting for PG1's SOC */
    *pg[2].CONL |= CONL_ON;
    *pg[1].CONL |= CONL_ON;
    *pg[0].CONL |= CONL_ON;
    outputs(true);
}

/* ---------- PWM interrupt: one new sample per PWM period (20 kHz) ---------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _PWM1Interrupt(void)
{
    phaseAcc += phaseInc;
    uint8_t idx = (uint8_t)(phaseAcc >> 24);              /* top 8 bits -> table */
    const uint8_t off[3] = { 0, 85, 171 };                /* 0, 120, 240 degrees */
    for (int i = 0; i < 3; i++) {
        int32_t s = (int32_t)sineTab[(uint8_t)(idx + off[i])] * depthQ15 >> 15;  /* -32767..32767 */
        uint16_t dc = (uint16_t)((PER / 2) + (((int32_t)(PER / 2) * s) >> 15));
        *pg[i].DC = dc;
    }
    PG1STATbits.UPDREQ = 1;            /* PG1 is master; UPDREQ on each so the */
    PG2STATbits.UPDREQ = 1;            /* three load their new duty together   */
    PG3STATbits.UPDREQ = 1;
    pwmIsrCount++;
    IFS4bits.PWM1IF = 0;
}

void setModulationHz(double hz)
{
    uint32_t inc = (uint32_t)(hz * 4294967296.0 / (double)PWM_HZ);
    noInterrupts(); phaseInc = inc; interrupts();
}

/* ---------- measure the PWM frequency on RB14 with SCCP2 input capture ---------- */
double measurePwmHz(void)
{
    /* PPS: SCCP2 input capture <- RP46 (RB14). Input register ICM2R (RPINR4). */
    __builtin_write_RPCON(0x0000);
    RPINR4bits.ICM2R = 46;
    __builtin_write_RPCON(0x0800);

    PMD2bits.CCP2MD = 0;
    CCP2CON1L = 0;                    /* off; also gives D6's PWM back if Lab 14-style code ran */
    CCP2CON1H = 0; CCP2CON2L = 0; CCP2CON3H = 0;
    CCP2CON2H = 0x0000;               /* ICS = 000: capture input = IC2 = the PPS pin (RB14) */
    CCP2CON1L = 0x0031u;              /* MOD=0001 every rising edge, CCSEL=1 capture, T32=1 32-bit,
                                       * CLKSEL=000 FOSC/2 = FCY, prescaler 1:1 */
    CCP2CON1Lbits.CCPON = 1;

    uint32_t t[9]; int n = 0;
    unsigned long t0 = millis();
    while (n < 9 && millis() - t0 < 50) {
        if (CCP2STATLbits.ICBNE) {
            uint16_t lo = CCP2BUFL; uint16_t hi = CCP2BUFH;
            t[n++] = ((uint32_t)hi << 16) | lo;
        }
    }
    CCP2CON1Lbits.CCPON = 0;
    if (n < 9) return 0.0;
    uint32_t span = t[8] - t[0];      /* 8 periods */
    return span ? 8.0 * (double)FCY / (double)span : 0.0;
}

/* ---------- serial helpers ---------- */
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

double modHz = 1.0; int depthPc = 80; int deadNs = 500; bool running = true;

void status(void)
{
    Serial.print("# PER="); Serial.print(PER);
    Serial.print(" (expect "); Serial.print(PWM_HZ); Serial.print(" Hz)  measured=");
    double f = measurePwmHz();
    if (f > 0) { Serial.print(f, 1); Serial.print(" Hz"); }
    else Serial.print("no edges on RB14");
    Serial.print("  mod="); Serial.print(modHz, 1);
    Serial.print(" Hz depth="); Serial.print(depthPc);
    Serial.print("% dead="); Serial.print(deadNs);
    Serial.print(" ns  "); Serial.print(running ? "RUNNING" : "STOPPED");
    Serial.print("  isr/s~"); Serial.println(pwmIsrCount);
    pwmIsrCount = 0;
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    for (int i = 0; i < SINE_N; i++) sineTab[i] = (int16_t)(32767.0 * sin(2.0 * M_PI * i / SINE_N));

    setModulationHz(modHz);
    pwmBegin();
    Serial.println("Lab 16 - 3-phase sine PWM on PG1..PG3 (J2-4..J2-9). f<hz> m<%> d<ns> s g a ?");
    delay(1100);
    status();
    double f = measurePwmHz();
    if (f > 0 && fabs(f - (double)PWM_HZ) > PWM_HZ / 10)
        Serial.println("# NOTE: measured != 20 kHz -> the PWM clock is not FCY; scale PER accordingly");
}

void loop()
{
    digitalWrite(LED_BUILTIN, running && ((millis() / 250) % 2) ? LED_ON : LED_OFF);

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'f') { long v = readNumber(); if (v >= 1 && v <= 100) { modHz = v; setModulationHz(modHz); } status(); }
        else if (c == 'm') { long v = readNumber(); if (v <= 100) { depthPc = (int)v; depthQ15 = (uint16_t)(v * 32767L / 100); } status(); }
        else if (c == 'd') { long v = readNumber(); if (v <= 2000) { deadNs = (int)v; setDeadTime((uint16_t)v); } status(); }
        else if (c == 's') { outputs(false); running = false; status(); }
        else if (c == 'g') { outputs(true);  running = true;  status(); }
        else if (c == '?') status();
        else if (c == 'a') {
            delay(20); while (Serial.available()) Serial.read();
            while (Serial.available() == 0) { Serial.println(analogRead(A0)); delay(5); }
            while (Serial.available()) Serial.read();
        }
    }
}
