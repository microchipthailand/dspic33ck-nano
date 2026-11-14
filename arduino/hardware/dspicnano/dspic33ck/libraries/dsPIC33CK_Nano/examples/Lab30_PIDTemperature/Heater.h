/*
 * Heater.h - temperature sensor, heater output and a simulated heater (Lab 30)
 *
 *   heaterBegin();                 once, in setup()
 *   heaterSet(percent);            heater power 0..100 % (PWM on D5)
 *   float t = readTemperature();   degC, averaged
 *   heaterSafe(t)                  false = too hot or sensor broken: heater forced OFF
 *   heaterSimulate(true);          no hardware: a built-in model of a heater
 *
 * HARDWARE
 *   Sensor  MCP9700A (or TMP36): VOUT -> A0 = RA0 (J2-15), VDD -> 3V3 (J3-1), GND
 *           VOUT = 0.5 V + 10 mV per degC   (0.75 V at 25 degC, 1.0 V at 50 degC)
 *           Fix it ON the heater resistor (Kapton tape / thermal glue).
 *   Heater  10 ohm 5 W resistor from 5V (J2-1) to the drain of a logic-level
 *           N-MOSFET (IRLB8721, IRLZ44N, AO3400 ...). Source -> GND.
 *           Gate <- 100 ohm <- D5 = RB0 (J3-21), plus 10 k gate -> GND.
 *           5 V / 10 ohm = 0.5 A = 2.5 W at 100 %: it gets HOT (> 80 degC). Don't touch.
 *           Needs a USB-C supply of at least 1 A.
 *
 * SENSOR MATHS
 *   analogRead() gives 0..1023 for 0..3.3 V, so 1 step = 3.2 mV = 0.32 degC.
 *   Averaging 64 readings (with a little noise) gives a smoother, finer value.
 *
 * SIMULATED HEATER (heaterSimulate(true))
 *   Two lags in a row, like the real thing:
 *     heater   dTh/dt = (Tamb + K * power - Th) / TAU_HEATER   K = 60 degC at 100 %
 *     sensor   dTs/dt = (Th - Ts) / TAU_SENSOR                 (the sensor lags behind)
 *   plus a little noise and the same 0.32 degC ADC steps. Use it to learn
 *   PID tuning without any parts; SIM_SPEED > 1 runs it faster than real time.
 */
#ifndef LAB30_HEATER_H
#define LAB30_HEATER_H

#include <Arduino.h>

const uint8_t PIN_HEATER = 5;            /* D5 = RB0 (J3-21), PWM */
const uint8_t PIN_TEMP   = A0;           /* RA0 (J2-15) */

#define HEATER_MAX_C   90.0f             /* above this: heater OFF (safety) */
#define SENSOR_MIN_C  -20.0f             /* below: sensor missing / shorted */
#define SENSOR_MAX_C  125.0f

/* simulated heater */
#ifndef SIM_SPEED
#define SIM_SPEED      1.0f              /* 1 = real time */
#endif
#define SIM_K          60.0f             /* degC rise at 100 % */
#define SIM_TAU_HEATER 60.0f             /* s */
#define SIM_TAU_SENSOR  8.0f             /* s */
#define SIM_AMBIENT    25.0f             /* degC */

bool  heaterSim = false;
float heaterPower = 0;                   /* % now */
static float simTh = SIM_AMBIENT, simTs = SIM_AMBIENT;
static unsigned long simLastMs = 0;
static uint32_t simRng = 12345;

void heaterSet(float percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    heaterPower = percent;
    if (!heaterSim) analogWrite(PIN_HEATER, (int)(percent * 2.55f + 0.5f));
}

void heaterBegin(void)
{
    pinMode(PIN_TEMP, INPUT);
    heaterSet(0);
}

void heaterSimulate(bool on)
{
    heaterSim = on;
    analogWrite(PIN_HEATER, 0);           /* the real heater stays off in simulation */
    simTh = simTs = SIM_AMBIENT;
    simLastMs = millis();
    heaterSet(heaterPower);
}

static float simNoise(void)              /* about +-0.15 degC */
{
    simRng = simRng * 1103515245UL + 12345UL;
    return ((int)((simRng >> 16) & 0xFF) - 128) / 850.0f;
}

float readTemperature(void)
{
    if (heaterSim) {
        unsigned long now = millis();
        float dt = (now - simLastMs) / 1000.0f * SIM_SPEED;
        simLastMs = now;
        while (dt > 0) {                  /* small steps keep the model stable */
            float h = dt > 0.05f ? 0.05f : dt;
            simTh += (SIM_AMBIENT + SIM_K * heaterPower / 100.0f - simTh) / SIM_TAU_HEATER * h;
            simTs += (simTh - simTs) / SIM_TAU_SENSOR * h;
            dt -= h;
        }
        float t = simTs + simNoise();
        return (int)(t / 0.0322f + 0.5f) * 0.0322f;   /* like averaged ADC steps */
    }
    long sum = 0;
    for (int i = 0; i < 64; i++) sum += analogRead(PIN_TEMP);
    float volts = sum / 64.0f * 3.3f / 1023.0f;
    return (volts - 0.5f) / 0.010f;       /* MCP9700A / TMP36 */
}

/* false = heater must stay OFF: too hot, or the sensor reads nonsense */
bool heaterSafe(float t)
{
    if (t > HEATER_MAX_C || t < SENSOR_MIN_C || t > SENSOR_MAX_C) {
        heaterSet(0);
        return false;
    }
    return true;
}

#endif /* LAB30_HEATER_H */
