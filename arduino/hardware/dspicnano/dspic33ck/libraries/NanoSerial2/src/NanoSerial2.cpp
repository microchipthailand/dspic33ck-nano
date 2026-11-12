/*
 * NanoSerial2.cpp - UART2 driver (see NanoSerial2.h)
 *
 * Mirrors the core's HardwareSerial.c for UART1, which is hardware-verified on
 * this silicon: fractional baud generator (baud = FCY / BRG), UARTEN set before
 * UTXEN/URXEN, TX latch driven high before the pin becomes an output.
 * PPS codes from p33CK256MC005.h: _RPOUT_U2TX = 3, input register _U2RXR.
 */
#include "NanoSerial2.h"

static volatile uint8_t  _rx[SERIAL2_RX_BUFFER];
static volatile uint16_t _head, _tail;
static volatile unsigned long _ovr;
static uint8_t _de = SERIAL2_NO_DE;

/* PPS output: RP32..RP77 are packed two per RPORx word starting at RPOR0,
 * RPn in the low byte (even n) or high byte (odd n), 6-bit function code. */
static void _pps_out(int16_t rp, uint8_t fn)
{
    volatile uint16_t *rpor = &RPOR0 + ((rp - 32) / 2);
    if (rp & 1) *rpor = (uint16_t)((*rpor & 0x00FFu) | ((uint16_t)fn << 8));
    else        *rpor = (uint16_t)((*rpor & 0xFF00u) | fn);
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    while (!U2STAHbits.URXBE) {               /* drain the hardware FIFO */
        uint8_t c = (uint8_t)U2RXREG;
        uint16_t next = (uint16_t)((_head + 1u) % SERIAL2_RX_BUFFER);
        if (next != _tail) { _rx[_head] = c; _head = next; }
        else               { _ovr++; }
    }
    if (U2STAbits.OERR) U2STAbits.OERR = 0;   /* clear overrun so RX restarts */
    IFS1bits.U2RXIF = 0;
}

extern "C" uint8_t serial2_begin(unsigned long baud, uint8_t rx, uint8_t tx, uint8_t de)
{
    int16_t rp_rx = pinToRP(rx);
    int16_t rp_tx = pinToRP(tx);
    if (rp_rx < 0 || rp_tx < 0) return 0;      /* PORTA has no PPS */

    serial2_end();
    _de = de;
    if (_de != SERIAL2_NO_DE) { pinMode(_de, OUTPUT); digitalWrite(_de, LOW); }   /* receive */

    pinMode(rx, INPUT);                        /* also clears ANSEL */
    digitalWrite(tx, HIGH);                    /* idle high before it drives */
    pinMode(tx, OUTPUT);

    __builtin_write_RPCON(0x0000);
    _pps_out(rp_tx, 3);                        /* _RPOUT_U2TX */
    _U2RXR = (uint16_t)rp_rx;
    __builtin_write_RPCON(0x0800);

    U2MODE = 0; U2MODEH = 0; U2STA = 0; U2STAH = 0;
    U2MODEHbits.BCLKSEL = 0b00;                /* baud clock = FCY */
    U2MODEHbits.BCLKMOD = 1;                   /* fractional: baud = FCY / BRG */
    if (baud == 0UL) baud = 9600UL;
    {
        unsigned long brg = (FCY + baud / 2UL) / baud;
        if (brg < 1UL) brg = 1UL;
        if (brg > 0xFFFFFUL) brg = 0xFFFFFUL;
        U2BRG  = (uint16_t)(brg & 0xFFFFUL);
        U2BRGH = (uint16_t)((brg >> 16) & 0x000FUL);
    }
    _head = 0; _tail = 0; _ovr = 0;
    IPC6bits.U2RXIP = 3;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;

    U2MODEbits.UARTEN = 1;                     /* UARTEN first, then TX/RX enables */
    U2MODEbits.UTXEN = 1;
    U2MODEbits.URXEN = 1;
    return 1;
}

extern "C" void serial2_end(void)
{
    if (U2MODEbits.UARTEN) serial2_flush();
    IEC1bits.U2RXIE = 0;
    U2MODEbits.UARTEN = 0;
    _head = 0; _tail = 0;
}

extern "C" int serial2_available(void)
{
    return (int)((SERIAL2_RX_BUFFER + _head - _tail) % SERIAL2_RX_BUFFER);
}

extern "C" int serial2_read(void)
{
    if (_head == _tail) return -1;
    uint8_t c = _rx[_tail];
    _tail = (uint16_t)((_tail + 1u) % SERIAL2_RX_BUFFER);
    return c;
}

extern "C" int serial2_peek(void)
{
    return (_head == _tail) ? -1 : (int)_rx[_tail];
}

extern "C" void serial2_flush(void)
{
    while (!U2STAbits.TRMT) { }                /* shift register empty = last stop bit sent */
    if (_de != SERIAL2_NO_DE) digitalWrite(_de, LOW);   /* RS485 back to receive */
}

extern "C" size_t serial2_write(uint8_t c)
{
    if (_de != SERIAL2_NO_DE) digitalWrite(_de, HIGH);  /* RS485 driver on */
    while (U2STAHbits.UTXBF) { }               /* wait for FIFO space */
    U2TXREG = c;
    return 1;
}

extern "C" unsigned long serial2_overruns(void) { return _ovr; }

static size_t _print_u(unsigned long v, int base)
{
    char buf[33];
    int i = 0;
    size_t n = 0;
    if (base < 2) base = 10;
    do {
        int d = (int)(v % (unsigned long)base);
        buf[i++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v /= (unsigned long)base;
    } while (v);
    while (i) n += serial2_write((uint8_t)buf[--i]);
    return n;
}

size_t NanoSerial2::print(unsigned long v, int base) { return _print_u(v, base); }

size_t NanoSerial2::print(long v, int base)
{
    if (v < 0 && base == DEC) { serial2_write('-'); return 1 + _print_u((unsigned long)(-v), base); }
    return _print_u((unsigned long)v, base);
}

NanoSerial2 Serial2;
