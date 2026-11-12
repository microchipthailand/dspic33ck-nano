/*
 * Lab 01 - Blink the USER LED
 * Board: dsPIC33CK Nano (Tools > Board > dsPIC33CK Nano)
 *
 * Goal:  your first program - make the on-board USER LED flash.
 *
 * Hardware (nothing to wire):
 *   USER LED D3 = RC13 = Arduino pin 34 = LED_BUILTIN
 *   The LED is ACTIVE LOW: 3V3 -> 1k -> LED -> RC13, so writing LOW lights it.
 *   LED_ON / LED_OFF from the board package hide that detail.
 *
 * Try:   change the delays; make an SOS pattern (... --- ...).
 */

#include <Arduino.h>

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    digitalWrite(LED_BUILTIN, LED_ON);
    delay(200);
    digitalWrite(LED_BUILTIN, LED_OFF);
    delay(800);
}
