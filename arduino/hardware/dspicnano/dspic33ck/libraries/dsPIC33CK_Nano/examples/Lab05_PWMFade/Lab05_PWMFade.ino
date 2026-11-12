/*
 * Lab 05 - PWM: fade an external LED, brightness from a pot
 * Board: dsPIC33CK Nano
 *
 * Goal:  analogWrite() on a PWM pin; map() an ADC reading to a duty cycle.
 *
 * PWM pins on this board (SCCP1-4, ~490 Hz):
 *   D5 = RB0 (J3-21)   D6 = RB1 (J3-20)   D7 = RB2 (J3-17)   D8 = RB3 (J3-16)
 *   The USER LED (RC13) is NOT a PWM pin.
 *
 * Wiring (breadboard):
 *   LED anode -> 330 ohm -> D5 (RB0, J3-21);  LED cathode -> GND
 *   Optional: pot wiper on RA0 (J2-15) like Lab 04. Hold USER to use the pot,
 *   release to auto-fade.
 */

#include <Arduino.h>

const uint8_t PWM_PIN = 5;   /* RB0 */

void setup()
{
    pinMode(PWM_PIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
}

void loop()
{
    if (digitalRead(BUTTON_BUILTIN) == LOW) {
        /* manual: pot sets brightness */
        int duty = map(analogRead(A0), 0, 1023, 0, 255);
        analogWrite(PWM_PIN, duty);
        delay(10);
        return;
    }

    /* automatic: breathe up and down */
    for (int duty = 0; duty <= 255; duty += 5) {
        analogWrite(PWM_PIN, duty);
        delay(15);
    }
    for (int duty = 255; duty >= 0; duty -= 5) {
        analogWrite(PWM_PIN, duty);
        delay(15);
    }
}
