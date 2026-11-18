/*
 * nano_bootloader.h - bootloader support for MPLAB X / VS Code (XC-DSC) applications on the dsPIC33CK Nano.
 *
 * The serial bootloader (J5, 115200 baud) opens a 300 ms window after every reset. NanoBootloader.exe / nanobl.exe
 * gets the board into that window by sending a 10-byte "soft entry" sequence to the RUNNING application, which must
 * then reset itself. Two ways to support that, pick ONE:
 *
 *   A. Your application uses UART1 (RC10 = TX J5-2, RC11 = RX J5-3) at any baud rate:
 *        call NanoBL_RxByte(b) for every byte you receive on UART1 (e.g. in the U1RX interrupt or an
 *        MCC UART1 receive callback, right after reading U1RXREG / UART1_Read()).
 *
 *   B. Your application does not use UART1:
 *        call NanoBL_UartInit(FCY) once after the clock is set up, and NanoBL_Task() regularly from the main loop.
 *        UART1 is then only listened to for the soft-entry sequence (115200 baud, receive only).
 *
 * Without either, the board can still be updated with manual entry: hold USER, press and release RESET, keep USER
 * held ~1 s (the LED stays on).
 *
 * Linking: the application must be linked with p33CK256MC005-app.gld and the linker option --no-ivt
 * (see README.md). Config words: include nano_bl_config.h in ONE source file (or use matching #pragma config).
 */
#ifndef NANO_BOOTLOADER_H
#define NANO_BOOTLOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Feed one received UART1 byte. Resets into the bootloader when the soft-entry sequence completes. */
void NanoBL_RxByte(uint8_t b);

/* Reset into the bootloader now (e.g. from your own "update" command). Does not return. */
void NanoBL_EnterBootloader(void);

/* Option B: UART1 receive-only at 115200 on RC11 (RP59), polled. fcy = instruction clock in Hz. */
void NanoBL_UartInit(unsigned long fcy);
void NanoBL_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* NANO_BOOTLOADER_H */
