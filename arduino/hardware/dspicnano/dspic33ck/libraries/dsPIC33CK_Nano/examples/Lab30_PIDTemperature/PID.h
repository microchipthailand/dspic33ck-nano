/*
 * PID.h - a PID controller with anti-windup and a relay autotune (Lab 30)
 *
 *   PIDset(kp, ki, kd);           gains
 *   PIDlimits(0, 100);            output range (here: heater power in %)
 *   out = PIDcompute(set, temp);  call it at a steady rate (here every 100 ms)
 *   PIDreset(out);                start from this output (bumpless switch from manual)
 *
 *   pidP, pidI, pidD              the three parts of the last output, to plot
 *
 *   Autotune (relay method):
 *   PIDtuneStart(set);            the output now jumps between max and min
 *   out = PIDtuneStep(temp);      call it instead of PIDcompute()
 *   PIDtuneDone()                 true when it has measured enough
 *   PIDtuneOK()                   true if the result is usable -> gains are set
 *   tuneKu, tunePu                the measured ultimate gain / period (s)
 *
 * ---------------------------------------------------------------------------
 * THE PID FORMULA (every call, dt = time since the last call)
 *   error e = setpoint - measured
 *   P = kp * e                         now:     the bigger the error, the more power
 *   I = I + ki * e * dt                past:    the error that stayed builds up
 *   D = -kd * d(measured)/dt           future:  slows down when it heats up fast
 *   output = P + I + D, limited to outMin .. outMax
 *
 * Three details that make it work in practice:
 *   - D is taken from the MEASUREMENT, not the error, so a new setpoint does
 *     not give a huge kick ("derivative kick"). It is also low-pass filtered
 *     (time constant PID_D_FILTER s), because the sensor is noisy.
 *   - ANTI-WINDUP: while the output is stuck at a limit, I stops growing in
 *     that direction. Otherwise I builds up during a long warm-up at 100 %
 *     and the temperature overshoots badly.
 *   - Units here: e in degC, output in %. So kp = % per degC,
 *     ki = % per degC per second, kd = % per (degC per second).
 *
 * AUTOTUNE (Astrom-Hagglund relay test)
 *   The output jumps to max when the temperature is below the setpoint and
 *   to min above it (with a small hysteresis). The temperature then swings
 *   around the setpoint. From the swing:
 *     a  = half the peak-to-peak swing (degC)    Pu = period of one swing (s)
 *     d  = half the output step (%)
 *     Ku = 4 d / (pi a)     the gain at which a P controller would oscillate
 *   Gains with the "some overshoot" Ziegler-Nichols rule:
 *     kp = 0.33 Ku    ki = kp / (Pu / 2)    kd = kp * Pu / 3
 *   (Classic ZN, kp = 0.6 Ku, is faster but overshoots more and amplifies
 *    sensor noise.)
 * ---------------------------------------------------------------------------
 */
#ifndef LAB30_PID_H
#define LAB30_PID_H

#include <Arduino.h>
#include <math.h>

#define PID_D_FILTER    2.0f              /* s, low-pass on the D part */
#ifndef PID_NOW_MS
#define PID_NOW_MS()    millis()          /* the clock the PID uses (a test can speed it up) */
#endif

float pidKp = 8.0f, pidKi = 0.08f, pidKd = 40.0f;
float pidMin = 0.0f, pidMax = 100.0f;
float pidP = 0, pidI = 0, pidD = 0;       /* parts of the last output */

static float         pidLastPv = 0, pidDfilt = 0;
static bool          pidFirst = true;
static unsigned long pidLastMs = 0;

void PIDset(float kp, float ki, float kd) { pidKp = kp; pidKi = ki; pidKd = kd; }
void PIDlimits(float lo, float hi)        { pidMin = lo; pidMax = hi; }

/* forget the history; the I part starts at 'output' (bumpless start) */
void PIDreset(float output)
{
    pidI = output < pidMin ? pidMin : output > pidMax ? pidMax : output;
    pidDfilt = 0;
    pidFirst = true;
}

float PIDcompute(float setpoint, float measured)
{
    unsigned long now = PID_NOW_MS();
    float dt = (now - pidLastMs) / 1000.0f;
    pidLastMs = now;
    if (pidFirst || dt <= 0.0f || dt > 5.0f) {     /* first call: no history yet */
        pidFirst = false;
        pidLastPv = measured;
        dt = 0.0f;
    }
    float e = setpoint - measured;

    /* D: rate of change of the measurement, low-pass filtered */
    if (dt > 0.0f) {
        float rate = (measured - pidLastPv) / dt;
        pidDfilt += dt / (dt + PID_D_FILTER) * (rate - pidDfilt);
    }
    pidLastPv = measured;

    pidP = pidKp * e;
    float iStep = pidKi * e * dt;
    pidI += iStep;
    if (pidI > pidMax) pidI = pidMax;
    if (pidI < pidMin) pidI = pidMin;
    pidD = -pidKd * pidDfilt;

    float out = pidP + pidI + pidD;
    /* anti-windup: at a limit, take back an I step that pushes further out */
    if (out > pidMax) { if (iStep > 0) pidI -= iStep; out = pidMax; }
    else if (out < pidMin) { if (iStep < 0) pidI -= iStep; out = pidMin; }
    return out;
}

/* ========================= relay autotune ========================= */
#define TUNE_HYST    0.3f                 /* degC: ignore noise around the setpoint */
#define TUNE_CYCLES  4                    /* full swings to measure (the first is skipped) */

float tuneKu = 0, tunePu = 0;
static float tuneSet, tuneOut, tuneMax, tuneMin;
static float tuneHiSum, tuneLoSum;
static unsigned long tuneLastUp, tunePerSum;
static int  tuneUps, tuneCount;
static bool tuneDone = true, tuneOk = false;
static unsigned long tuneStartMs;

void PIDtuneStart(float setpoint)
{
    tuneSet = setpoint; tuneOut = pidMax;
    tuneMax = -1e9f; tuneMin = 1e9f;
    tuneHiSum = tuneLoSum = 0; tunePerSum = 0;
    tuneUps = 0; tuneCount = 0; tuneLastUp = 0;
    tuneDone = false; tuneOk = false;
    tuneStartMs = PID_NOW_MS();
}

bool PIDtuneDone(void) { return tuneDone; }
bool PIDtuneOK(void)   { return tuneOk; }

float PIDtuneStep(float measured)
{
    if (tuneDone) return pidMin;
    if (PID_NOW_MS() - tuneStartMs > 3600000UL) { tuneDone = true; return pidMin; }   /* 1 h: give up */
    if (measured > tuneMax) tuneMax = measured;
    if (measured < tuneMin) tuneMin = measured;

    if (tuneOut == pidMax && measured > tuneSet + TUNE_HYST) {
        tuneOut = pidMin;                 /* switch down; the low of this swing starts here */
        tuneMin = measured;
    } else if (tuneOut == pidMin && measured < tuneSet - TUNE_HYST) {
        tuneOut = pidMax;                 /* switch up = start of a new swing */
        unsigned long now = PID_NOW_MS();
        tuneUps++;
        if (tuneUps >= 3) {               /* the swing before this one is complete */
            tunePerSum += now - tuneLastUp;
            tuneHiSum += tuneMax;
            tuneLoSum += tuneMin;
            tuneCount++;
        }
        tuneLastUp = now;
        tuneMax = measured;               /* the high of the new swing starts here */
        if (tuneCount >= TUNE_CYCLES - 1) {
            float a  = (tuneHiSum - tuneLoSum) / tuneCount / 2.0f;
            float d  = (pidMax - pidMin) / 2.0f;
            tunePu = tunePerSum / 1000.0f / tuneCount;
            tuneKu = a > 0.01f ? 4.0f * d / (3.14159265f * a) : 0;
            tuneDone = true;
            tuneOk = tuneKu > 0 && tunePu > 0.5f;
            if (tuneOk) {
                float kp = 0.33f * tuneKu;
                PIDset(kp, kp / (tunePu / 2.0f), kp * tunePu / 3.0f);
            }
            return pidMin;
        }
    }
    return tuneOut;
}

#endif /* LAB30_PID_H */
