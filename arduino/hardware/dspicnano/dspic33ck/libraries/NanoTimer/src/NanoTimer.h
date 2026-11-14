/*
 * NanoTimer.h - simple periodic timer interrupts for the dsPIC33CK Nano board
 *
 *   #include <NanoTimer.h>
 *
 *   void tick() { ... }                  // runs in an interrupt: keep it short
 *
 *   Timer1.begin(1000, tick);            // call tick() every 1000 us (1 kHz), starts now
 *   Timer1.stop();                       // pause
 *   Timer1.start();                      // run again, count restarts from 0
 *   Timer1.setPeriod(500);               // new period in microseconds
 *   Timer1.setFrequency(2.5);            // ... or in Hz
 *
 * Three timers: Timer1, Timer2, Timer3. They are the chip's SCCP1, SCCP2 and
 * SCCP3 modules, each used as one 32-bit timer clocked at FCY:
 *   period         1 tick = 1 / FCY  (10 ns at 100 MIPS, 250 ns at 4 MIPS)
 *   longest period 2^32 / FCY        (about 42 s at 100 MIPS, 17 min at 4 MIPS)
 *   shortest       10 us             (shorter periods leave no time for loop())
 *
 * NOTE
 *  - These are NOT the chip's own "Timer1" (T1CON). That one runs millis() and
 *    delay() and is never touched by this library.
 *  - Timer1/2/3 share hardware with analogWrite() on D5/D6/D7. Use either the
 *    timer or analogWrite on that pin, not both.
 *  - Timer4 does not exist here: SCCP4 belongs to tone().
 *  - The callback runs inside an interrupt. Variables it changes must be
 *    'volatile'. Do not call delay() or Serial.print() from it.
 */
#ifndef NANOTIMER_H
#define NANOTIMER_H

#include <Arduino.h>

class NanoTimer {
public:
    /* set up, attach the callback (may be 0) and start */
    void begin(unsigned long periodUs, void (*callback)(void) = 0);

    void start(void);                 /* run, counting from 0 */
    void stop(void);                  /* pause, keeps the count */
    void resume(void);                /* continue where stop() left it */
    bool isRunning(void) const { return _running; }

    void setPeriod(unsigned long periodUs);   /* microseconds */
    void setFrequency(float hz);              /* interrupts per second */
    unsigned long getPeriod(void) const;      /* microseconds (rounded) */

    void attachInterrupt(void (*callback)(void));
    void detachInterrupt(void);
    void setPriority(uint8_t level);  /* 1 (low) .. 7 (high), default 3 */

    unsigned long read(void);         /* microseconds since the last interrupt */
    unsigned long count(void);        /* interrupts since begin() */
    void resetCount(void);

    /* used by the interrupt handlers - not for sketches */
    void _isr(void) { _count++; if (_fn) _fn(); }

private:
    /* No constructor on purpose: every member starts as 0 from .bss and the
     * module number is worked out from the object's address. (Written when
     * this core did not yet run C++ global constructors - fixed in 1.0.1 by
     * ldscripts/keep_ctors.ld - and kept: it needs no start-up code at all.) */
    uint8_t idx(void) const;          /* 0 = SCCP1, 1 = SCCP2, 2 = SCCP3 */
    void setTicks(uint32_t ticks);
    void enableIrq(bool on);
    void (*volatile _fn)(void);
    uint32_t _ticks;                  /* period in FCY clocks */
    volatile unsigned long _count;
    bool _running;
};

extern NanoTimer Timer1;
extern NanoTimer Timer2;
extern NanoTimer Timer3;

#endif
