/*
 * wiring_private.h - core-internal contract between the wiring_*.c files
 *
 * NOT part of the Arduino API, and deliberately NOT included from Arduino.h --
 * a sketch has no business calling any of this. It exists because tone() and
 * analogWrite() have to share one piece of hardware, and therefore have to be
 * able to evict each other.
 *
 * ------------------------------------------------------------------------
 * SCCP4 POLICY: LAST CALLER WINS
 * ------------------------------------------------------------------------
 * This family has exactly one general-purpose timer of its own -- Timer1, which
 * wiring.c owns for millis(). There is no T2CON..T9CON. Every other time base
 * on the device belongs to an SCCP module, and SCCP1-4 are precisely the four
 * analogWrite() PWM channels (the intersection across all four supported
 * devices is CCP1-4 and no more). tone() therefore has to borrow one, and it
 * borrows SCCP4 -- the channel behind D8 on every board in this family.
 *
 * The consequence mirrors the caveat stock AVR Arduino carries for pins 3 and
 * 11: a tone and analogWrite(8, ...) cannot both be live.
 *
 *   tone(anyPin, f)  while D8 is running PWM   -> the PWM on D8 stops
 *   analogWrite(8, v) while a tone is playing  -> the tone stops
 *
 * Neither call fails, warns, or returns an error -- the most recent one wins.
 * analogWrite() on any other pin is unaffected, and so is the tone if
 * analogWrite() is called on D5, D6 or D7.
 */

#ifndef WIRING_PRIVATE_H
#define WIRING_PRIVATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clear the Peripheral Module Disable bits for every SCCP the device has, once.
 * Must be called before ANY CCPx register write: with PMD set, writes to the
 * module are silently dropped. Idempotent. Lives in wiring_analog.c. */
void _sccp_pmd_enable(void);

/* Stop PWM on one pin and hand the pin back to the GPIO: CCPON = 0 and the PPS
 * output mapping torn down. A no-op for a pin that has no PWM running, and for
 * a pin that is not a PWM pin at all. Lives in wiring_analog.c. */
void _pwm_disable_pin(uint8_t pin);

/* Stop any tone that is playing and park its pin as an OUTPUT driving LOW.
 * A no-op if no tone is active. Lives in wiring_tone.c; analogWrite() calls it
 * before touching SCCP4, which is what implements the policy above. */
void _tone_release(void);

#ifdef __cplusplus
}
#endif

#endif /* WIRING_PRIVATE_H */
