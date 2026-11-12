/*
 * bl_time.h - a free-running tick for timeouts, on Timer1.
 *
 * Timer1 with a 1:256 prescaler off FCY = 4 MHz gives 15625 ticks per second:
 * 16 us resolution, and the 16-bit counter wraps every 4.19 s. Timeouts are
 * compared with unsigned subtraction so a wrap costs nothing, but no timeout
 * may exceed 4.19 s - which is why "wait forever" is its own case rather than
 * a very large tick count.
 *
 * A timer rather than a cycle-counting delay loop, because the same tick is
 * used for the entry window, the inter-byte frame timeout and the idle LED
 * blink, and because a delay loop's timing changes with every optimiser
 * decision.
 */

#ifndef BL_TIME_H
#define BL_TIME_H

#include <stdint.h>

#define BL_TICKS_PER_SEC    15625UL
#define BL_MS_TO_TICKS(ms)  ((uint16_t)((BL_TICKS_PER_SEC * (uint32_t)(ms)) / 1000UL))

void     bl_time_init(void);
void     bl_time_deinit(void);
uint16_t bl_ticks(void);

/* Elapsed ticks since `start`, correct across the 16-bit wrap. */
static inline uint16_t bl_ticks_since(uint16_t start)
{
    return (uint16_t)(bl_ticks() - start);
}

#endif /* BL_TIME_H */
