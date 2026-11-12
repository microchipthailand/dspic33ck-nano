/*
 * Lab 06 - External interrupt on the USER button
 * Board: dsPIC33CK Nano
 *
 * Goal:  attachInterrupt() - react to the button without polling it.
 *        The main loop blinks slowly and never reads the button; the ISR
 *        counts presses and the loop reports them.
 *
 * Hardware (nothing to wire): USER button RC12 (pin 33), LED RC13 (pin 34).
 * Serial Monitor 115200 via J5.
 *
 * Rules for ISRs: keep them short, share data through 'volatile' variables,
 * and do not call Serial or delay() inside them.
 */

#include <Arduino.h>

volatile unsigned int  pressCount = 0;
volatile unsigned long lastIsrMs  = 0;

void onButton()
{
    unsigned long now = millis();
    if (now - lastIsrMs > 50) {       /* simple debounce inside the ISR */
        pressCount++;
        lastIsrMs = now;
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 06 - press USER, the interrupt counts it");
    attachInterrupt(digitalPinToInterrupt(BUTTON_BUILTIN), onButton, FALLING);
}

void loop()
{
    static unsigned int reported = 0;

    noInterrupts();                   /* read the shared value atomically */
    unsigned int n = pressCount;
    interrupts();

    if (n != reported) {
        reported = n;
        Serial.print("presses: ");
        Serial.println(n);
    }

    /* slow heartbeat proves the loop is busy doing something else */
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2 ? LED_ON : LED_OFF);
}
