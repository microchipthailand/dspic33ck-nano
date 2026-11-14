/*
 * Lab 11 - Timers: run your own code at an exact rate with Timer1/2/3
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * A timer calls a function of yours at a fixed rate, in the background, no
 * matter what loop() is doing (even during delay()). The NanoTimer library
 * makes this a few lines:
 *
 *   Timer1.begin(1000, tick);   // call tick() every 1000 us = 1000 times/s
 *   Timer1.stop();              // pause it
 *   Timer1.start();             // run again
 *   Timer1.setPeriod(500);      // change the period (us), or setFrequency(Hz)
 *
 * This lab runs three timers at once, each toggling its own pin:
 *   Timer1  every   1 ms -> D9  = RB4 (J3-15)  500 Hz square wave (piezo / scope)
 *   Timer2  every  10 ms -> D10 = RB5 (J3-11)   50 Hz
 *   Timer3  every 100 ms -> D11 = RB6 (J3-10)    5 Hz blink (put an LED here)
 * Once a second loop() prints how many times each timer fired, measured
 * against millis().
 *
 * Wiring (optional - it runs with no parts):
 *   LED + 1k from D11 = RB6 (J3-10) to GND -> blinks at 5 Hz
 *   piezo from D9 = RB4 (J3-15) to GND     -> 500 Hz tone made by Timer1
 *   scope / logic analyser on J3-15, J3-11, J3-10
 *   (RB5/RB6 are also the ICSP PGD/PGC lines - fine while the program runs,
 *    unplug the PICkit if it loads them.)
 *
 * Serial commands (115200):
 *   1, 2, 3    stop / start that timer
 *   s          stop all          g   start (go) all
 *   f <Hz>     set Timer1's rate, e.g. "f 2000" -> the piezo tone goes up
 *   ?          show the settings
 *
 * Good to know:
 *   - The library's Timer1/2/3 are the chip's SCCP1/2/3 modules. The chip's own
 *     "Timer1" (T1CON) runs millis() and delay(); the library does not touch it.
 *   - Timer1/2/3 share hardware with analogWrite() on D5/D6/D7: use one or the
 *     other on each. tone() uses SCCP4, so it still works.
 *   - The timer functions run inside an INTERRUPT: keep them short, do not use
 *     delay() or Serial there, and mark shared variables 'volatile'.
 */

#include <Arduino.h>
#include <NanoTimer.h>

const uint8_t PIN_T1 = 9;     /* RB4 */
const uint8_t PIN_T2 = 10;    /* RB5 */
const uint8_t PIN_T3 = 11;    /* RB6 */

/* ---- the functions the timers call (interrupt context) ---- */
void tick1() { digitalWrite(PIN_T1, !digitalRead(PIN_T1)); }
void tick2() { digitalWrite(PIN_T2, !digitalRead(PIN_T2)); }
void tick3() { digitalWrite(PIN_T3, !digitalRead(PIN_T3)); }

NanoTimer *const timers[3] = { &Timer1, &Timer2, &Timer3 };
unsigned long last, prev[3];     /* for the once-a-second report */

void showSettings()
{
    for (int i = 0; i < 3; i++) {
        Serial.print("# Timer"); Serial.print(i + 1);
        Serial.print(": period "); Serial.print(timers[i]->getPeriod());
        Serial.print(" us, "); Serial.println(timers[i]->isRunning() ? "running" : "stopped");
    }
}

long readNumber()
{
    long v = 0; unsigned long t0 = millis();
    while (millis() - t0 < 60) {
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c == ' ') { Serial.read(); continue; }
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0'); t0 = millis();
        }
    }
    return v;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Lab 11 - Timers: Timer1/2/3 call tick1/2/3. Send 1 2 3 s g f <Hz> ?");
    pinMode(PIN_T1, OUTPUT);
    pinMode(PIN_T2, OUTPUT);
    pinMode(PIN_T3, OUTPUT);

    Timer1.begin(1000, tick1);      /*   1 ms */
    Timer2.begin(10000, tick2);     /*  10 ms */
    Timer3.begin(100000, tick3);    /* 100 ms */
    showSettings();
    last = millis();                /* first report covers a full second */
}

void loop()
{
    /* once a second: how often did each timer fire? */
    if (millis() - last >= 1000) {
        unsigned long now = millis();
        Serial.print("# per second:");
        for (int i = 0; i < 3; i++) {
            unsigned long c = timers[i]->count();
            Serial.print("  Timer"); Serial.print(i + 1); Serial.print(" = ");
            Serial.print((c - prev[i]) * 1000.0 / (now - last), 1);
            prev[i] = c;
        }
        Serial.println();
        last = now;
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c >= '1' && c <= '3') {
            NanoTimer *t = timers[c - '1'];
            if (t->isRunning()) t->stop(); else t->start();
            Serial.print("# Timer"); Serial.print((char)c);
            Serial.println(t->isRunning() ? " started" : " stopped");
        } else if (c == 's') {
            for (int i = 0; i < 3; i++) timers[i]->stop();
            Serial.println("# all timers stopped (millis() still runs)");
        } else if (c == 'g') {
            for (int i = 0; i < 3; i++) timers[i]->start();
            Serial.println("# all timers started");
        } else if (c == 'f') {
            long hz = readNumber();
            if (hz >= 1 && hz <= 50000) {
                Timer1.setFrequency(hz);
                Serial.print("# Timer1 -> "); Serial.print(hz); Serial.print(" Hz, period ");
                Serial.print(Timer1.getPeriod()); Serial.println(" us (D9 toggles at half that rate)");
            } else {
                Serial.println("# f <Hz>: 1 .. 50000");
            }
        } else if (c == '?') {
            showSettings();
        }
    }
}
