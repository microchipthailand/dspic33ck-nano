/*
 * NanoBlink_BL - template MPLAB X / VS Code (XC-DSC) application for the dsPIC33CK Nano serial bootloader.
 *
 *  - links with p33CK256MC005-app.gld + --no-ivt, so it starts at 0x001800 behind the bootloader
 *  - switches FRC -> PLL, FCY = 100 MHz (the device always starts on the bootloader's FRC config)
 *  - Timer1 interrupt every 1 ms (interrupts reach it through the bootloader's IVT and the app trampoline)
 *  - LED RC13 (active low) blinks at 1 Hz; UART1 115200 on J5 prints the uptime and echoes what you type
 *  - every received byte goes to NanoBL_RxByte(), so NanoBootloader.exe can reset it into the bootloader
 *  - type 'b' to reset into the bootloader by hand
 */
#include <xc.h>
#include <stdint.h>
#include "nano_bootloader.h"
#include "nano_bl_config.h"          /* config words identical to the bootloader's (they are never written over serial) */

#define FCY 100000000UL

volatile uint32_t g_ms;              /* incremented by the Timer1 interrupt */
volatile uint8_t  g_rx[32];
volatile uint8_t  g_rx_head, g_rx_tail;

static void clock_init(void)
{
    /* FRC 8 MHz / N1 2 * M 100 = 400 MHz VCO, / N2 2 / N3 1 = 200 MHz FOSC -> FCY 100 MHz */
    CLKDIVbits.PLLPRE = 0b001;
    PLLFBDbits.PLLFBDIV = 100;
    PLLDIVbits.POST1DIV = 2;
    PLLDIVbits.POST2DIV = 1;
    __builtin_write_OSCCONH(0x01);               /* NOSC = FRCPLL */
    __builtin_write_OSCCONL(OSCCON | 0x01);      /* request the switch (FCKSM = CSECMD allows it) */
    while (OSCCONbits.COSC != 0b001) { }
    while (OSCCONbits.LOCK != 1) { }
}

static void uart1_init(unsigned long baud)
{
    unsigned long brg = (FCY + baud / 2UL) / baud;  /* fractional baud generator: baud = FCY / BRG */

    LATCbits.LATC10 = 1;                          /* idle high before the pin becomes an output */
    TRISCbits.TRISC10 = 0;                        /* RC10 = U1TX (RP58), J5-2 */
    TRISCbits.TRISC11 = 1;                        /* RC11 = U1RX (RP59), J5-3 */
    __builtin_write_RPCON(0x0000);
    _RP58R = 1;                                   /* U1TX */
    _U1RXR = 59;
    __builtin_write_RPCON(0x0800);

    U1MODEbits.UARTEN = 0;
    U1MODEHbits.BCLKSEL = 0b00;
    U1MODEHbits.BCLKMOD = 1;
    U1MODEbits.BRGH = 0;
    U1BRG  = (uint16_t)(brg & 0xFFFFUL);
    U1BRGH = (uint16_t)((brg >> 16) & 0x000FUL);
    U1MODEbits.MOD = 0b0000;
    IPC2bits.U1RXIP = 3;
    IFS0bits.U1RXIF = 0;
    IEC0bits.U1RXIE = 1;
    U1MODEbits.UARTEN = 1;                        /* UARTEN first, then UTXEN / URXEN */
    U1MODEbits.UTXEN = 1;
    U1MODEbits.URXEN = 1;
}

static void uart1_putc(char c)
{
    while (U1STAHbits.UTXBF) { }                  /* transmit buffer full */
    U1TXREG = (uint8_t)c;
}

static void uart1_puts(const char *s)
{
    while (*s) uart1_putc(*s++);
}

static void uart1_putu(uint32_t v)
{
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    uart1_puts(&b[i]);
}

static void timer1_init(void)
{
    T1CON = 0;
    T1CONbits.TCKPS = 0b01;                       /* FCY / 8 = 12.5 MHz */
    PR1 = 12500u - 1u;                            /* 1 ms */
    TMR1 = 0;
    IPC0bits.T1IP = 2;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

void __attribute__((interrupt, auto_psv)) _T1Interrupt(void)
{
    g_ms++;
    IFS0bits.T1IF = 0;
}

void __attribute__((interrupt, auto_psv)) _U1RXInterrupt(void)
{
    while (!U1STAHbits.URXBE) {
        uint8_t b = (uint8_t)U1RXREG;
        uint8_t next = (uint8_t)((g_rx_head + 1u) % sizeof(g_rx));
        if (next != g_rx_tail) { g_rx[g_rx_head] = b; g_rx_head = next; }
        NanoBL_RxByte(b);                         /* lets the host reset us into the bootloader */
    }
    if (U1STAbits.OERR) U1STAbits.OERR = 0;
    IFS0bits.U1RXIF = 0;
}

static uint32_t millis(void)
{
    uint32_t t;
    IEC0bits.T1IE = 0; t = g_ms; IEC0bits.T1IE = 1;
    return t;
}

int main(void)
{
    uint32_t next_blink = 0, next_print = 1000;

    clock_init();
    ANSELA = 0; ANSELB = 0; ANSELC = 0;
    LATCbits.LATC13 = 1;                          /* LED off (active low) */
    TRISCbits.TRISC13 = 0;
    uart1_init(115200UL);
    timer1_init();
    /* GIE resets to 0 on this device (DSPIC33CK256MC005.PIC: INTCON2 POR 000----0----0000), with or without the
     * bootloader: nothing interrupts until it is set. MCC projects do this in INTERRUPT_GlobalEnable(). */
    INTCON2bits.GIE = 1;

    uart1_puts("\r\nNanoBlink_BL: MPLAB X application started by the serial bootloader, FCY 100 MHz\r\n");
    uart1_puts("type 'b' to reset into the bootloader\r\n");

    for (;;) {
        uint32_t now = millis();
        if ((int32_t)(now - next_blink) >= 0) { LATCbits.LATC13 ^= 1; next_blink += 500u; }
        if ((int32_t)(now - next_print) >= 0) {
            uart1_puts("uptime "); uart1_putu(now / 1000u); uart1_puts(" s\r\n");
            next_print += 1000u;
        }
        while (g_rx_tail != g_rx_head) {
            uint8_t c = g_rx[g_rx_tail];
            g_rx_tail = (uint8_t)((g_rx_tail + 1u) % sizeof(g_rx));
            if (c == 'b') { uart1_puts("resetting into the bootloader\r\n"); while (!U1STAbits.TRMT) { } NanoBL_EnterBootloader(); }
            uart1_putc((char)c);
        }
    }
}
