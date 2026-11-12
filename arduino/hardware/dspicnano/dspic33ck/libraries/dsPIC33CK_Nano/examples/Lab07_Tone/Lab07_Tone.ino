/*
 * Lab 07 - Play a melody with tone()
 * Board: dsPIC33CK Nano
 *
 * Goal:  generate audio frequencies; press USER to play the tune.
 *
 * Wiring (breadboard):
 *   Passive piezo buzzer (or 8 ohm speaker through 100 ohm): + to D9 (RB4, J3-15),
 *   - to GND.
 *
 * Note: tone() uses timer SCCP4, which is also the PWM channel of D8 (RB3).
 *       Don't use analogWrite(8, ...) while a tone is playing.
 */

#include <Arduino.h>

const uint8_t BUZZER_PIN = 9;   /* RB4 */

/* note frequencies in Hz */
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523

const unsigned int melody[]   = { NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_G4, NOTE_E4, NOTE_C4, 0 };
const unsigned int duration[] = { 200,     200,     200,     400,     200,     200,     400,     200 };
const int NOTES = sizeof(melody) / sizeof(melody[0]);

void playMelody()
{
    for (int i = 0; i < NOTES; i++) {
        digitalWrite(LED_BUILTIN, melody[i] ? LED_ON : LED_OFF);
        if (melody[i]) {
            tone(BUZZER_PIN, melody[i], duration[i]);
        }
        delay(duration[i] * 13 / 10);  /* small gap between notes */
        noTone(BUZZER_PIN);
    }
    digitalWrite(LED_BUILTIN, LED_OFF);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(BUTTON_BUILTIN, INPUT);
    playMelody();                      /* once at start-up */
}

void loop()
{
    if (digitalRead(BUTTON_BUILTIN) == LOW) {
        playMelody();
        while (digitalRead(BUTTON_BUILTIN) == LOW) { }   /* wait for release */
    }
}
