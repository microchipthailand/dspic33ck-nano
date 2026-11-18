/*
 * nano_bootloader.c - soft entry into the dsPIC33CK Nano serial bootloader (see nano_bootloader.h).
 * The sequence is BL_SOFT_ENTRY_MAGIC of bootloaders/dspic33ck_nano/bl_config.h: eight improbable bytes followed by
 * their CRC-16, so a garbled or truncated prefix cannot reset a running application.
 */
#include <xc.h>
#include "nano_bootloader.h"

static const uint8_t magic[10] = { 0x1B, 0xF0, 0x33, 0x43, 0x4B, 0x21, 0x9E, 0x57, 0xE8, 0x3B };
static uint8_t pos;

void NanoBL_EnterBootloader(void)
{
    /* A software reset, not a jump: the bootloader expects reset-default clock and peripherals. */
    __asm__ volatile ("reset");
}

void NanoBL_RxByte(uint8_t b)
{
    if (b == magic[pos]) {
        if (++pos >= sizeof(magic)) {
            NanoBL_EnterBootloader();
        }
    } else {
        pos = (b == magic[0]) ? 1u : 0u;    /* this byte may start a new attempt */
    }
}

void NanoBL_UartInit(unsigned long fcy)
{
    unsigned long brg = (fcy + 57600UL) / 115200UL;     /* fractional baud generator: baud = FCY / BRG */

    TRISCbits.TRISC11 = 1;                              /* RC11 = U1RX (RP59), J5-3 */
    __builtin_write_RPCON(0x0000);                      /* unlock PPS */
    _U1RXR = 59;
    __builtin_write_RPCON(0x0800);                      /* lock PPS */

    U1MODEbits.UARTEN = 0;
    U1MODEHbits.BCLKSEL = 0b00;                         /* baud clock = FCY */
    U1MODEHbits.BCLKMOD = 1;                            /* fractional */
    U1MODEbits.BRGH = 0;
    U1BRG  = (uint16_t)(brg & 0xFFFFUL);
    U1BRGH = (uint16_t)((brg >> 16) & 0x000FUL);
    U1MODEbits.MOD = 0b0000;                            /* 8N1 */
    U1MODEbits.UARTEN = 1;                              /* UARTEN before URXEN, or URXEN does not stick */
    U1MODEbits.URXEN = 1;
}

void NanoBL_Task(void)
{
    if (U1STAbits.OERR) {
        U1STAbits.OERR = 0;                             /* overrun: drop it, keep listening */
    }
    while (!U1STAHbits.URXBE) {                         /* receive buffer not empty */
        NanoBL_RxByte((uint8_t)U1RXREG);
    }
}
