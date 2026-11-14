/*
 * NanoServo.h - hobby servo pulses for the dsPIC33CK Nano board
 *
 *   #include <NanoServo.h>
 *   NanoServo s;
 *   s.attach(5);                    // any remappable pin (PORTB / PORTC / PORTD)
 *   s.write(90);                    // 0..180 degrees (positional servo)
 *   s.writeMicroseconds(1500);      // or the pulse width directly
 *   s.detach();                     // pulses stop, the pin is driven LOW
 *
 * Same calls as the Arduino Servo library, so most servo sketches port by
 * changing "Servo" to "NanoServo".
 *
 * HOW: every servo gets its own SCCP module in dual-edge compare mode. The
 * hardware makes the 50 Hz (20 ms) frame and the 0.5..2.5 ms pulse by itself:
 * no interrupts, no jitter, and the CPU is free. A new width is buffered and
 * takes effect at the start of the next frame, so a pulse is never cut short.
 *   Resolution: 0.64 us at 100 MIPS (1:64 prescaler), 1 us at 4 MIPS (1:4).
 *
 * Up to 4 servos. The modules are handed out in this order:
 *   1st attach -> SCCP3   2nd -> SCCP2   3rd -> SCCP1   4th -> SCCP4
 * While a servo holds a module, do not use what else lives on it:
 *   SCCP3 = NanoTimer Timer3, analogWrite(D7)
 *   SCCP2 = NanoTimer Timer2, analogWrite(D6)
 *   SCCP1 = NanoTimer Timer1, analogWrite(D5)
 *   SCCP4 = tone(),           analogWrite(D8)
 * With 1-3 servos tone() keeps working.
 *
 * CONTINUOUS-ROTATION servos (FS90R, SG90-360, MG90-360 ...) read the same
 * pulse as a SPEED: ~1500 us = stop, 1000 us = full one way, 2000 us = full
 * the other way. Each one stops at a slightly different width ("trim").
 *
 * No constructor is needed: a global NanoServo starts zeroed = detached.
 * (Written when this core did not yet run C++ global constructors; it still
 * needs none, so it works the same with or without them.)
 */
#ifndef NANOSERVO_H
#define NANOSERVO_H

#include <Arduino.h>

#define MIN_PULSE_WIDTH       544     /* Arduino Servo defaults, used by write(angle) */
#define MAX_PULSE_WIDTH      2400
#define DEFAULT_PULSE_WIDTH  1500
#define REFRESH_INTERVAL    20000     /* us, 50 Hz frame */
#define NANOSERVO_MAX           4

class NanoServo {
public:
    /* returns 1..4 (the servo's channel) or 0 if the pin cannot be remapped
     * (RA0-RA4) or all four SCCP modules are taken */
    uint8_t attach(uint8_t pin);
    uint8_t attach(uint8_t pin, int minUs, int maxUs);
    void    detach(void);
    bool    attached(void) const { return _sccp != 0; }

    void    write(int value);              /* < 200: angle 0..180, else microseconds */
    void    writeMicroseconds(int us);     /* clamped to 400..2600 */
    int     read(void) const;              /* angle 0..180 */
    int     readMicroseconds(void) const { return _us; }

    uint8_t module(void) const { return _sccp; }   /* SCCP number 1..4, 0 = detached */

private:
    uint8_t  _sccp;          /* 0 = detached */
    uint8_t  _pin;
    int16_t  _rp;
    int16_t  _min, _max;
    int16_t  _us;
};

#endif
