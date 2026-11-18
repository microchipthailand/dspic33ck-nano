/*
 * wiring_shift.c - shiftOut/shiftIn and pulseIn for dsPIC33CK
 */

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (bitOrder == LSBFIRST) {
            digitalWrite(dataPin, val & 1);
            val >>= 1;
        } else {
            digitalWrite(dataPin, (val & 128) != 0);
            val <<= 1;
        }
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    uint8_t value = 0;
    uint8_t i;
    for (i = 0; i < 8; i++) {
        digitalWrite(clockPin, HIGH);
        if (bitOrder == LSBFIRST) {
            value |= digitalRead(dataPin) << i;
        } else {
            value |= digitalRead(dataPin) << (7 - i);
        }
        digitalWrite(clockPin, LOW);
    }
    return value;
}

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    unsigned long start = micros();
    unsigned long pulse_start, pulse_end;

    /* Wait for any previous pulse to end */
    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) return 0;
    }

    /* Wait for pulse to start */
    while (digitalRead(pin) != state) {
        if ((micros() - start) >= timeout) return 0;
    }
    pulse_start = micros();

    /* Wait for pulse to end */
    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) return 0;
    }
    pulse_end = micros();

    return pulse_end - pulse_start;
}

#ifdef __cplusplus
}
#endif
