/*
 * bl_uart.h - polled UART1 on the CDC pins of the Curiosity Nano.
 */

#ifndef BL_UART_H
#define BL_UART_H

#include <stdint.h>

void bl_uart_init(void);

/* Returns UART1, PPS and the two pins to their reset state, so the sketch's own
 * Serial.begin() starts from the same place it would after a power-on reset. */
void bl_uart_deinit(void);

void bl_uart_putc(uint8_t c);
void bl_uart_write(const uint8_t *buf, uint16_t len);
void bl_uart_flush(void);       /* blocks until the last bit has left the pin */

/* 1 and *c set if a byte arrived within `timeout` ticks, 0 on timeout.
 *
 * Every wait is bounded: there is no "wait forever" call. Waiting for the host
 * indefinitely is the caller's loop, which is also where the idle LED blinks,
 * so no wait inside this module can hang the bootloader on its own. */
int bl_uart_getc(uint16_t timeout, uint8_t *c);

#endif /* BL_UART_H */
