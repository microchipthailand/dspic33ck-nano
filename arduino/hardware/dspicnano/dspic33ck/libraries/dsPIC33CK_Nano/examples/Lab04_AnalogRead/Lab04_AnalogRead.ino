/*
 * Lab 04 - Read a potentiometer with the 12-bit ADC
 * Board: dsPIC33CK Nano
 *
 * Goal:  analogRead(), convert to volts, and show the level on the LED.
 *
 * Wiring (breadboard):
 *   10k potentiometer: ends to 3V3 (J3-1) and GND (J3-2), wiper to RA0 (J2-15)
 *   RA0 = A0 = analog channel AN0
 *
 * analogRead() returns 0..1023 (the 12-bit result scaled to Arduino range).
 * Full scale = AVDD = 3.3 V.
 * Serial Monitor 115200 (adapter on J5) - or open Tools > Serial Plotter.
 */

#include <Arduino.h>

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("Lab 04 - pot on RA0 (A0)");
}

void loop()
{
    int raw = analogRead(A0);
    double volts = raw * 3.3 / 1023.0;

    Serial.print("raw=");
    Serial.print(raw);
    Serial.print("  volts=");
    Serial.println(volts, 3);

    /* LED lights when the pot is above half scale */
    digitalWrite(LED_BUILTIN, raw > 512 ? LED_ON : LED_OFF);
    delay(200);
}
