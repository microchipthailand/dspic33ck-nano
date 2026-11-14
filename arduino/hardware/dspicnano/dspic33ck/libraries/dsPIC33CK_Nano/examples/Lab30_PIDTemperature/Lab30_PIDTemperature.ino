/*
 * Lab 30 - PID temperature control
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  hold a heater at a set temperature with a PID controller. See what
 *        P, I and D each do, tune it by hand, then let AUTOTUNE find the
 *        gains. Watch everything as curves in the Arduino Serial Plotter.
 *
 * The code is in three tabs:
 *   Lab30_PIDTemperature.ino   this file: the control loop and the commands
 *   PID.h                      the PID controller and the autotune
 *   Heater.h                   sensor, heater output, safety, simulated heater
 *
 * Hardware (or none: send x to use the simulated heater):
 *   MCP9700A / TMP36  VOUT -> A0 = RA0 (J2-15), VDD -> 3V3 (J3-1), GND -> GND
 *   10 ohm 5 W resistor = the heater, from 5V (J2-1) to a logic-level N-MOSFET
 *   (IRLB8721 / IRLZ44N / AO3400): drain <- resistor, source -> GND,
 *   gate <- 100 ohm <- D5 = RB0 (J3-21), 10 k from gate to GND.
 *   Tape the sensor onto the resistor. It gets HOT: don't touch it.
 *   Use a USB-C supply of at least 1 A (the heater takes 0.5 A).
 *
 * Serial Monitor / Serial Plotter at 115200, "Newline". Commands:
 *   a          AUTO: the PID holds the setpoint
 *   s<degC>    setpoint, e.g. s50 (20 .. 80)
 *   p<n> i<n> d<n>   gains, e.g. p8  i0.08  d40
 *   m<%>       MANUAL: fixed heater power, e.g. m30 (no control)
 *   o          heater OFF
 *   t          AUTOTUNE at the setpoint, then AUTO with the new gains
 *   x          simulated heater on / off (no hardware needed)
 *   v          plot also the P, I and D parts
 *   ?          status
 *
 * Plotter lines look like  set:50.00,temp:48.12,power:35.2
 * Lines starting with # are messages.
 */

#include <Arduino.h>
#include "PID.h"
#include "Heater.h"

const unsigned long LOOP_MS = 100;        /* control rate: 10 times a second */
const unsigned long PLOT_MS = 500;        /* plot rate */

enum Mode { OFF, AUTO, MANUAL, TUNE, FAULT };
const char *const MODE_NAME[] = { "OFF", "AUTO (PID)", "MANUAL", "AUTOTUNE", "FAULT - heater off" };
Mode  mode = OFF;
float setpoint = 50.0f;
float manualPower = 0;
float temperature = 0;
bool  showParts = false;

/* reads a number after a command letter, e.g. the "0.08" of "i0.08" */
float readNumber(bool *ok)
{
    char buf[16]; int n = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 50 && n < 15) {
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (!((c >= '0' && c <= '9') || c == '.' || c == '-')) break;
            buf[n++] = (char)Serial.read();
            t0 = millis();
        }
    }
    buf[n] = 0;
    *ok = n > 0;
    return n ? (float)atof(buf) : 0.0f;
}

void printGains()
{
    Serial.print("# kp="); Serial.print(pidKp, 3);
    Serial.print("  ki="); Serial.print(pidKi, 4);
    Serial.print("  kd="); Serial.println(pidKd, 2);
}

void status()
{
    Serial.print("# mode: "); Serial.print(MODE_NAME[mode]);
    Serial.print(heaterSim ? "  [SIMULATED heater]" : "  [real heater]");
    Serial.print("  set "); Serial.print(setpoint, 1);
    Serial.print(" C  temp "); Serial.print(temperature, 2);
    Serial.print(" C  power "); Serial.print(heaterPower, 1); Serial.println(" %");
    printGains();
}

void setMode(Mode m)
{
    if (m == AUTO) PIDreset(heaterPower);   /* bumpless: continue from the present power */
    if (m == TUNE) PIDtuneStart(setpoint);
    if (m == OFF || m == FAULT) heaterSet(0);
    mode = m;
    Serial.print("# mode: "); Serial.println(MODE_NAME[m]);
}

void command(int c)
{
    bool ok;
    float v;
    switch (c) {
    case 'a': setMode(AUTO); break;
    case 'o': setMode(OFF); break;
    case 't': Serial.print("# autotune at "); Serial.print(setpoint, 1);
              Serial.println(" C: the power now jumps 100 % / 0 % - this takes a few swings ...");
              setMode(TUNE); break;
    case 'x': heaterSimulate(!heaterSim);
              Serial.println(heaterSim ? "# SIMULATED heater (no hardware needed)" : "# real heater");
              setMode(OFF); break;
    case 'v': showParts = !showParts; break;
    case '?': status(); break;
    case 's': v = readNumber(&ok);
              if (ok && v >= 20 && v <= 80) { setpoint = v; Serial.print("# setpoint "); Serial.print(v, 1); Serial.println(" C"); }
              else Serial.println("# s<20..80>, e.g. s50");
              break;
    case 'm': v = readNumber(&ok);
              if (ok && v >= 0 && v <= 100) { manualPower = v; setMode(MANUAL); }
              else Serial.println("# m<0..100>, e.g. m30");
              break;
    case 'p': v = readNumber(&ok); if (ok && v >= 0) PIDset(v, pidKi, pidKd); printGains(); break;
    case 'i': v = readNumber(&ok); if (ok && v >= 0) PIDset(pidKp, v, pidKd); printGains(); break;
    case 'd': v = readNumber(&ok); if (ok && v >= 0) PIDset(pidKp, pidKi, v); printGains(); break;
    }
}

/* one control step: measure, decide the power, switch the heater */
void controlStep()
{
    temperature = readTemperature();
    if (!heaterSafe(temperature)) {
        if (mode != FAULT) {
            setMode(FAULT);
            Serial.print("# SAFETY: temperature "); Serial.print(temperature, 1);
            Serial.println(" C is too high or the sensor is not connected. Send o, then a.");
        }
        return;
    }
    switch (mode) {
    case AUTO:   heaterSet(PIDcompute(setpoint, temperature)); break;
    case MANUAL: heaterSet(manualPower); break;
    case TUNE:
        heaterSet(PIDtuneStep(temperature));
        if (PIDtuneDone()) {
            if (PIDtuneOK()) {
                Serial.print("# autotune done: Ku="); Serial.print(tuneKu, 2);
                Serial.print("  Pu="); Serial.print(tunePu, 1); Serial.println(" s");
                printGains();
                setMode(AUTO);
            } else {
                Serial.println("# autotune FAILED (no clean swing) - gains not changed");
                setMode(OFF);
            }
        }
        break;
    default:     heaterSet(0); break;          /* OFF, FAULT */
    }
}

void plot()
{
    Serial.print("set:");     Serial.print(setpoint, 2);
    Serial.print(",temp:");   Serial.print(temperature, 2);
    Serial.print(",power:");  Serial.print(heaterPower, 1);
    if (showParts && mode == AUTO) {
        Serial.print(",P:"); Serial.print(pidP, 1);
        Serial.print(",I:"); Serial.print(pidI, 1);
        Serial.print(",D:"); Serial.print(pidD, 1);
    }
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    heaterBegin();
    PIDlimits(0, 100);
    delay(10);
    Serial.println("# Lab 30 - PID temperature control.  a s<C> p<n> i<n> d<n> m<%> o t x v ?");
    temperature = readTemperature();
    status();
    Serial.println("# send a to start (or x first: simulated heater, no hardware)");
}

void loop()
{
    static unsigned long tLoop = 0, tPlot = 0;
    unsigned long now = millis();

    if (now - tLoop >= LOOP_MS) {
        tLoop += LOOP_MS;
        if (now - tLoop >= LOOP_MS) tLoop = now;   /* fell behind (long print?): don't catch up in a burst */
        controlStep();
    }
    if (now - tPlot >= PLOT_MS) { tPlot = now; plot(); }

    /* LED: on = heating hard, off = idle, blinks in FAULT */
    if (mode == FAULT) digitalWrite(LED_BUILTIN, (now / 150) % 2 ? LED_ON : LED_OFF);
    else digitalWrite(LED_BUILTIN, heaterPower > 50 ? LED_ON : LED_OFF);

    if (Serial.available() > 0) command(Serial.read());
}
