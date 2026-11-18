/*
 * Lab 37 - High-resolution PWM: 6 independent channels
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required - the PWM clock comes from the PLL)
 *
 * Goal:  1. Run six PWM outputs with their own duty AND their own phase,
 *           from the three motor-control generators PG1-PG3.
 *        2. Get the finest edge steps this chip has: the PWM counts at
 *           FVCO/2 = 400 MHz (2.5 ns) instead of 100 MHz (10 ns).
 *        3. Understand resolution: bits = log2(ticks per period), and how it
 *           drops as the frequency goes up.
 *        4. Go below one tick on average with dithering (1/16 tick = 156 ps).
 *        5. See why it matters: six LEDs that fade down to almost nothing
 *           without visible steps, and a 6-phase interleaved pattern.
 *
 * IMPORTANT: this board's dsPIC33CK256MC005 has NO true high-resolution PWM
 * (the 250 ps option exists only on the dsPIC33CK MP parts - see the HRPWM
 * library). This lab gets the most out of the MC part: the fastest PWM clock
 * plus dithering. The PWM6.h tab explains the details.
 *
 * Hardware:
 *   Logic analyser / scope on J2-4 ... J2-9, or six LEDs, each with a 1k
 *   resistor to GND (J2-2):
 *     ch1 = RB14 J2-8   ch2 = RB15 J2-9   ch3 = RB12 J2-6
 *     ch4 = RB13 J2-7   ch5 = RB10 J2-4   ch6 = RB11 J2-5
 *   For the dither test: RC filter on ch1: J2-8 -> 10k -> node -> 1 uF -> GND,
 *   node -> A0 = RA0 (J2-15), then 'a' reads the filtered voltage.
 *   NEVER connect these pins to a power stage without a gate driver.
 *
 * Serial commands (Serial Monitor on J5, 115200, line ending "Newline"):
 *   ?             status: frequency, resolution, every channel
 *   r             resolution table for common frequencies
 *   f<hz>         frequency of all six            f20000
 *   d<ch> <%>     duty, 0.000 - 100 %             d1 25.125
 *   p<ch> <deg>   phase (where the pulse starts)  p2 90
 *   n<ch> <ns>    pulse width in nanoseconds      n3 1234.5
 *   t<ch> <ticks> pulse width in PWM ticks        t4 1000
 *   x<ch> <1/16>  dithered width, in 1/16 ticks   x1 16008  (= 1000.5 ticks)
 *   m             6-phase interleaved: 15 % each, 60 deg apart
 *   l             six LEDs breathing, 60 deg apart (l again = stop)
 *   a             read A0 (the RC-filtered ch1) for the dither test
 *   c             calibrate the PWM clock from a measurement of ch1
 *   s / g         stop (all forced low) / go
 *
 * ----------------------------------------------------------------------------
 * WHAT "RESOLUTION" MEANS HERE
 *   One period has T ticks. A duty can only be a whole number of ticks, so
 *   the duty step is 1/T and the resolution is log2(T) bits.
 *     T = f_clk / f_pwm        f_clk = 400 MHz:  10 kHz -> T = 40000 -> 15.3 bits
 *   With a 100 MHz clock (the usual FCY) the same 10 kHz has only 13.3 bits.
 *   Why it matters: at 100 kHz a 10 ns step is 0.1 % of the duty - in a power
 *   supply that is a visible voltage step; 2.5 ns is 0.025 %.
 * DITHERING
 *   Width 1000 ticks on 8 periods and 1001 on the other 8 -> the average is
 *   1000.5. The PG1 period interrupt does this with a 4-bit accumulator.
 *   A filter after the PWM (RC, LC, a motor winding, your eye) averages it.
 * Not verified: the simulator has no PWM model. setup() measures the real PWM
 * frequency with SCCP2 and corrects the clock value if it differs.
 * ----------------------------------------------------------------------------
 */

#if FCY < 50000000UL
#error "Lab37 needs Tools > Clock = 100 MIPS (the PWM clock FVCO/2 comes from the PLL)"
#endif

#include <Arduino.h>
#include <math.h>
#include "PWM6.h"

const unsigned long BAUD = 115200;

void printChannel(uint8_t ch)
{
    Serial.print("  ch"); Serial.print(ch + 1); Serial.print("  "); Serial.print(PWM6_PIN[ch]);
    Serial.print("  duty "); Serial.print(100.0 * pwm6Width[ch] / pwm6Ticks(), 3); Serial.print(" %");
    Serial.print("  width "); Serial.print(pwm6Width[ch]);
    if (ch == pwm6DitherCh) { Serial.print("+"); Serial.print(pwm6DitherFrac); Serial.print("/16"); }
    Serial.print(" ticks = "); Serial.print(pwm6Width[ch] * pwm6TickNs(), 1); Serial.print(" ns");
    Serial.print("  phase "); Serial.print(360.0 * pwm6Start[ch] / pwm6Ticks(), 1); Serial.println(" deg");
}

void status()
{
    Serial.print("# "); Serial.print(pwm6Hz(), 1); Serial.print(" Hz   period "); Serial.print(pwm6Ticks());
    Serial.print(" ticks of "); Serial.print(pwm6TickNs(), 3); Serial.print(" ns   resolution ");
    Serial.print(pwm6Bits(), 2); Serial.print(" bits (step "); Serial.print(100.0 / pwm6Ticks(), 4); Serial.print(" %)");
    if (pwm6DivShift) { Serial.print("   clock divider 1:"); Serial.print(1 << pwm6DivShift); }
    Serial.println(pwm6Running ? "" : "   STOPPED");
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) printChannel(ch);
    double m = pwm6Measure();
    Serial.print("  measured on ch1: ");
    if (m > 0) { Serial.print(m, 1); Serial.println(" Hz"); }
    else Serial.println("no edges (ch1 at 0 / 100 %, or stopped)");
}

void resolutionTable()
{
    double clk = pwm6ClkHz * (1 << pwm6DivShift);
    Serial.print("# resolution with this PWM clock ("); Serial.print(clk / 1e6, 1);
    Serial.println(" MHz) vs a 100 MHz clock:");
    const double F[] = { 1000, 6100, 10000, 20000, 50000, 100000, 200000, 500000, 1000000 };
    for (uint8_t i = 0; i < sizeof(F) / sizeof(F[0]); i++) {
        double t = clk / F[i], t100 = 100e6 / F[i];
        Serial.print("  "); Serial.print(F[i] / 1000, 1); Serial.print(" kHz   ");
        Serial.print(t > 65536 ? 16.0 : log(t) / log(2.0), 1); Serial.print(" bits");
        if (t > 65536) Serial.print(" (divider)");
        Serial.print("   at 100 MHz: "); Serial.print(t100 > 65536 ? 16.0 : log(t100) / log(2.0), 1); Serial.println(" bits");
    }
}

/* ---------------- demos ---------------- */
bool ledDemo = false;

void interleaved()
{
    ledDemo = false;
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) { pwm6Phase(ch, 60.0 * ch); pwm6Percent(ch, 15.0); }
    Serial.println("# 6-phase interleaved: each channel 15 %, starting 60 deg after the previous one.");
    Serial.println("  On a logic analyser the six pulses follow each other like a staircase -");
    Serial.println("  this is how multi-phase buck converters spread their current.");
}

void ledStep()
{
    /* brightness 0..1 as a sine, 60 deg apart; gamma 2.8 so it looks even to the eye.
     * At the dark end the width goes down to a few ticks: smooth only because a
     * tick is so short. */
    static unsigned long last = 0;
    if (millis() - last < 10) return;
    last = millis();
    double t = millis() / 3000.0 * 2 * M_PI;            /* one breath every 3 s */
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) {
        double b = 0.5 + 0.5 * sin(t - ch * M_PI / 3);
        pwm6Pulse(ch, 0, (uint16_t)(pow(b, 2.8) * pwm6Ticks()));
    }
}

void readFiltered()
{
    long sum = 0;
    for (int i = 0; i < 64; i++) { sum += analogRead(A0); delay(1); }
    double mv = sum / 64.0 * 3300.0 / 1024.0;     /* analogRead() = 10 bit */
    Serial.print("# A0 (RC-filtered ch1) = "); Serial.print(mv, 2);
    Serial.print(" mV   expected 3300 mV x duty = ");
    double w = pwm6Width[0] + (pwm6DitherCh == 0 ? pwm6DitherFrac / 16.0 : 0);
    Serial.print(3300.0 * w / pwm6Ticks(), 2); Serial.println(" mV");
}

void calibrate()
{
    double before = pwm6ClkHz;
    if (!pwm6Calibrate()) { Serial.println("# calibrate: no edges on ch1 - give ch1 a duty between 1 and 99 %"); return; }
    Serial.print("# PWM clock: nominal "); Serial.print(before / 1e6, 3); Serial.print(" MHz, measured ");
    Serial.print(pwm6ClkHz / 1e6, 3); Serial.println(" MHz");
    pwm6Frequency(pwm6Hz());                                /* same period, corrected ns values */
}

/* ---------------- commands ---------------- */
void help()
{
    Serial.println();
    Serial.println("Lab 37 - 6-channel PWM, finest resolution of the MC005 (FVCO/2 clock + dithering)");
    Serial.println("  ? r f<hz> d<ch> <%> p<ch> <deg> n<ch> <ns> t<ch> <ticks> x<ch> <1/16> m l a c s g");
}

void command(char *cmd)
{
    char op = cmd[0];
    char *rest = cmd + 1;
    long ch = strtol(rest, &rest, 10) - 1;                  /* channel for d/p/n/t/x */
    double v = strtod(rest, 0);
    bool chOk = ch >= 0 && ch < PWM6_CH;
    if (strchr("dpntx", op) && !chOk) { Serial.println("# channel must be 1..6, e.g. d1 25.5"); return; }
    if (strchr("dpntx", op)) ledDemo = false;
    switch (op) {
    case '?': status(); break;
    case 'r': resolutionTable(); break;
    case 'f': {
        double hz = strtod(cmd + 1, 0);
        if (hz < 400 || hz > 5000000) { Serial.println("# frequency 400 Hz .. 5 MHz"); break; }
        pwm6Frequency(hz); status(); break;
    }
    case 'd': pwm6Percent(ch, v); printChannel(ch); break;
    case 'p': pwm6Phase(ch, v); printChannel(ch); break;
    case 'n': pwm6Ns(ch, v); printChannel(ch); break;
    case 't': pwm6Pulse(ch, pwm6Start[ch], (uint16_t)v); printChannel(ch); break;
    case 'x': pwm6Dither(ch, (uint32_t)v); printChannel(ch);
              Serial.print("  average width "); Serial.print(v / 16.0, 4); Serial.print(" ticks = ");
              Serial.print(v / 16.0 * pwm6TickNs(), 3); Serial.println(" ns"); break;
    case 'm': interleaved(); break;
    case 'l': ledDemo = !ledDemo;
              if (ledDemo) for (uint8_t c = 0; c < PWM6_CH; c++) pwm6Phase(c, 0);
              Serial.println(ledDemo ? "# LEDs breathing" : "# LED demo stopped"); break;
    case 'a': readFiltered(); break;
    case 'c': calibrate(); break;
    case 's': pwm6Outputs(false); Serial.println("# all outputs forced LOW"); break;
    case 'g': pwm6Outputs(true);  Serial.println("# running"); break;
    default: help(); break;
    }
}

void setup()
{
    Serial.begin(BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
    pwm6Begin(10000);
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) pwm6Percent(ch, 10.0 * (ch + 1));   /* 10 .. 60 % */
    help();
    delay(5);
    double m = pwm6Measure();
    if (m > 0 && fabs(m - pwm6Hz()) > pwm6Hz() * 0.01) {
        Serial.print("# measured "); Serial.print(m, 1); Serial.print(" Hz instead of ");
        Serial.print(pwm6Hz(), 1); Serial.println(" Hz: the PWM clock differs from 400 MHz. Correcting:");
        calibrate();
        pwm6Frequency(10000);
    }
    status();
}

char line[32];
uint8_t lineLen = 0;

void loop()
{
    digitalWrite(LED_BUILTIN, pwm6Running && ((millis() / 500) & 1) ? LED_ON : LED_OFF);
    if (ledDemo) ledStep();
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
