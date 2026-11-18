/*
 * Lab 20 - Mini data logger (capstone)
 * Board: dsPIC33CK Nano
 *
 * Goal:  combine everything - timed sampling with millis(), several ADC
 *        channels, statistics, an alarm LED, button control and CSV output
 *        you can paste into a spreadsheet or watch in the Serial Plotter.
 *
 * Wiring (breadboard):
 *   CH0 = A0 = RA0 (J2-15) : potentiometer wiper (Lab 04)
 *   CH1 = A1 = RA1 (J2-16) : light sensor - LDR from 3V3 to RA1, 10k RA1 to GND
 *   CH2 = A2 = RA2 (J2-17) : anything 0..3.3 V (leave open to see noise)
 *   Optional buzzer on D9 (RB4) beeps on alarm.
 *
 * Controls:
 *   USER button : start / stop logging (LED blinks while logging)
 *   Serial 'r'  : reset statistics    's' : print statistics
 * Alarm: CH0 above ALARM_V volts -> LED solid on + beep.
 */

#include <Arduino.h>

const uint8_t  CH[]        = { A0, A1, A2 };
const int      NCH         = 3;
const unsigned long PERIOD = 250;     /* ms between samples (4 Hz) */
const double   ALARM_V     = 2.5;
const uint8_t  BUZZER_PIN  = 9;

bool logging = true;
unsigned long nextSample = 0;
unsigned long samples = 0;
int  minV[NCH], maxV[NCH];
long sumV[NCH];

double toVolts(int raw) { return raw * 3.3 / 1023.0; }

void resetStats()
{
    samples = 0;
    for (int i = 0; i < NCH; i++) { minV[i] = 1023; maxV[i] = 0; sumV[i] = 0; }
}

void printStats()
{
    Serial.print("# samples=");
    Serial.println(samples);
    for (int i = 0; i < NCH; i++) {
        Serial.print("# CH");
        Serial.print(i);
        Serial.print(" min=");
        Serial.print(toVolts(minV[i]), 3);
        Serial.print(" max=");
        Serial.print(toVolts(maxV[i]), 3);
        Serial.print(" avg=");
        Serial.println(samples ? toVolts((int)(sumV[i] / (long)samples)) : 0.0, 3);
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    resetStats();
    Serial.println("# Lab 20 - data logger. USER = start/stop, r = reset, s = stats");
    Serial.println("time_ms,ch0_V,ch1_V,ch2_V");
}

void loop()
{
    /* --- button: start/stop (edge-detect + debounce) --- */
    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        logging = !logging;
        Serial.println(logging ? "# logging ON" : "# logging OFF");
        delay(30);
    }
    lastBtn = btn;

    /* --- serial commands --- */
    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'r') { resetStats(); Serial.println("# stats reset"); }
        if (c == 's') { printStats(); }
    }

    /* --- timed sampling --- */
    if (logging && (long)(millis() - nextSample) >= 0) {
        nextSample = millis() + PERIOD;
        int v[NCH];
        for (int i = 0; i < NCH; i++) {
            v[i] = analogRead(CH[i]);
            if (v[i] < minV[i]) minV[i] = v[i];
            if (v[i] > maxV[i]) maxV[i] = v[i];
            sumV[i] += v[i];
        }
        samples++;

        Serial.print(millis());
        for (int i = 0; i < NCH; i++) {
            Serial.print(",");
            Serial.print(toVolts(v[i]), 3);
        }
        Serial.println();

        bool alarm = toVolts(v[0]) > ALARM_V;
        if (alarm) {
            digitalWrite(LED_BUILTIN, LED_ON);
            tone(BUZZER_PIN, 2000, 60);
        } else {
            digitalWrite(LED_BUILTIN, (samples & 1) ? LED_ON : LED_OFF);
        }
    }

    if (!logging) digitalWrite(LED_BUILTIN, LED_OFF);
}
