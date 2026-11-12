/*
 * Lab 02 - USER button controls the LED (with debounce)
 * Board: dsPIC33CK Nano
 *
 * Goal:  read a digital input, debounce it, and toggle the LED on each press.
 *
 * Hardware (nothing to wire):
 *   USER button SW2 = RC12 = pin 33 = BUTTON_BUILTIN
 *     The board has an external 10k pull-up (R6), so the pin reads HIGH when
 *     released and LOW when pressed.
 *   USER LED        = RC13 = pin 34 = LED_BUILTIN (active low)
 *
 * Try:   count presses; long-press (> 1 s) to blink fast.
 */

#include <Arduino.h>

const unsigned long DEBOUNCE_MS = 25;

int  stableState   = HIGH;          /* debounced button level */
int  lastReading   = HIGH;
unsigned long lastChange = 0;
bool ledOn = false;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(BUTTON_BUILTIN, INPUT);  /* external pull-up on the board */
}

void loop()
{
    int reading = digitalRead(BUTTON_BUILTIN);

    if (reading != lastReading) {      /* contact bounced or moved */
        lastChange  = millis();
        lastReading = reading;
    }

    if ((millis() - lastChange) > DEBOUNCE_MS && reading != stableState) {
        stableState = reading;
        if (stableState == LOW) {      /* pressed: falling edge */
            ledOn = !ledOn;
            digitalWrite(LED_BUILTIN, ledOn ? LED_ON : LED_OFF);
        }
    }
}
