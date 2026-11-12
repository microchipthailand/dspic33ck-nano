/*
 * HardwareSerial.c - UART1 implementation for dsPIC33CK Arduino core
 *
 * Uses UART1 with PPS auto-configured from variant pins_arduino.h:
 *   TX -> SERIAL_TX_RP_REG (PPS output function 0x01)
 *   RX -> SERIAL_RX_RP_NUM (PPS input source)
 *
 * Interrupt-driven receive with ring buffer.
 *
 * The serial_* functions below are the real implementation and have external
 * linkage. How they are reached depends on the language:
 *   - C   (MPLAB X projects): through the HardwareSerial_t function-pointer
 *          struct defined at the bottom of this file.
 *   - C++ (Arduino IDE):      through the inline HardwareSerial class in
 *          HardwareSerial.h, which gives real print()/println() overloads.
 * Both faces call exactly these functions, so there is one implementation.
 */

#include "Arduino.h"
#include "HardwareSerial.h"

#ifdef __cplusplus
extern "C" {
#endif

static ring_buffer_t _rx_buffer = { {0}, 0, 0 };

/* ============================================================
 * Serial bootloader soft entry
 *
 * The serial bootloader opens a ~300 ms window at every reset, and on the
 * Curiosity Nano the host has no way to cause a reset: DBG3 drives MCLR and only
 * the debugger drives DBG3, and the board has no reset button. So the host asks
 * the RUNNING SKETCH to reset itself, by sending the ten-byte sequence below;
 * this state machine recognises it in the RX interrupt and executes `reset`.
 *
 * The sequence is BL_SOFT_ENTRY_MAGIC from
 * bootloaders/dspic33ck256mc005/bl_config.h - the last two bytes are the CRC-16
 * of the first eight, so a garbled or truncated prefix cannot reset a running
 * sketch. _build/bootloader_check.sh asserts this copy is byte-identical to the
 * one the host sends. The matched bytes are still delivered to the ring buffer:
 * if the match never completes they were ordinary data and the sketch is
 * entitled to them, and if it does complete the reset makes the buffer moot.
 *
 * Cost when it never fires: one compare and one branch per received byte, and
 * 132 bytes of flash.
 *
 * COMPILED IN ONLY WHEN -DSERIAL_BOOTLOADER_ENTRY IS PASSED, which in practice
 * means only the MC005 "Bootloader: Serial (UART, 115200)" menu option. 1.0.3
 * shipped it unconditionally and so charged all four boards 132 bytes for a
 * feature three of them cannot use at all - they have no bootloader, so a sketch
 * on them can only ever be replaced with a debugger, and a sniffer that can reset
 * the board is then pure liability with no upside. It also silently broke the
 * promise that "Bootloader: none" builds byte-identically to 1.0.2.
 *
 * A sketch built without it can only be replaced by holding SW0 through a power
 * cycle, or with the debugger. That is the correct default for a board that has no
 * bootloader burned, because nothing else can program it either.
 * ============================================================ */
#ifdef SERIAL_BOOTLOADER_ENTRY
static const uint8_t _soft_entry_magic[10] = {
    0x1B, 0xF0, 0x33, 0x43, 0x4B, 0x21, 0x9E, 0x57, 0xE8, 0x3B
};
static uint8_t _soft_entry_pos = 0;

static void _soft_entry_byte(uint8_t b)
{
    if (b == _soft_entry_magic[_soft_entry_pos]) {
        _soft_entry_pos++;
        if (_soft_entry_pos >= sizeof(_soft_entry_magic)) {
            /* A software reset, not a jump: the bootloader expects the
             * reset-default clock and peripherals, which is exactly what `reset`
             * restores. Nothing is flushed first - the host is not listening for
             * anything but the bootloader now. */
            __asm__ volatile ("reset");
        }
    } else {
        /* Restart the match, but allow this byte to be a first byte: otherwise a
         * repeated prefix in the magic itself could not be resynchronised. */
        _soft_entry_pos = (b == _soft_entry_magic[0]) ? 1 : 0;
    }
}
#endif  /* SERIAL_BOOTLOADER_ENTRY */

/* ============================================================
 * UART1 RX Interrupt
 * ============================================================ */
void __attribute__((interrupt, auto_psv)) _U1RXInterrupt(void)
{
    uint16_t next_head = (_rx_buffer.head + 1) % SERIAL_BUFFER_SIZE;
    uint8_t  b = (uint8_t)U1RXREG;

    if (next_head != _rx_buffer.tail) {
        _rx_buffer.buffer[_rx_buffer.head] = b;
        _rx_buffer.head = next_head;
    }
#ifdef SERIAL_BOOTLOADER_ENTRY
    /* After the buffer, so a full buffer cannot stop an upload: a sketch that has
     * stopped calling read() is exactly the one you need to replace. */
    _soft_entry_byte(b);
#endif
    IFS0bits.U1RXIF = 0;
}

/* ============================================================
 * Implementation functions
 * ============================================================ */
void serial_begin(unsigned long baud)
{
    /* Idle high before the pin becomes an output. An asynchronous line idles
     * high, so driving it low even briefly looks like a start bit followed by a
     * break, and the first character out is then framed wrong. Microchip's own
     * driver carries the same warning next to its enable sequence. */
    digitalWrite(PIN_SERIAL_TX, HIGH);

    SERIAL_TX_TRIS = 0;
    SERIAL_RX_TRIS = 1;
#ifdef SERIAL_RX_ANSEL
    SERIAL_RX_ANSEL = 0;
#endif

    __builtin_write_RPCON(0x0000);
    SERIAL_TX_RP_REG = PPS_OUT_U1TX;
    _U1RXR = SERIAL_RX_RP_NUM;
    __builtin_write_RPCON(0x0800);

    U1MODEbits.UARTEN = 0;

    /* Baud rate, via the fractional baud generator.
     *
     * The 20-bit divisor in U1BRGH:U1BRG counts baud-clock cycles per bit
     * directly (baud = FCY / BRG), rather than the coarser prescaled forms
     * FCY/(16*(BRG+1)) with BRGH=0 or FCY/(4*(BRG+1)) with BRGH=1. This is the
     * mode Microchip's own MCC driver for these parts selects first, and the
     * finer step is what makes fast baud rates reachable on a slow clock: at
     * the 8 MHz FRC default (FCY = 4 MHz) the /16 divisor for 115200 lands on
     * 2, i.e. 125000 baud -- an 8.5% error that no receiver can decode.
     * Fractional gives BRG = 35 -> 114286 baud, -0.7%, comfortably in spec.
     *
     * Rounded rather than truncated: truncation alone throws away up to a full
     * count, which at small BRG values is several percent of the bit period.
     * The fallback prescaled modes are only needed below FCY/0xFFFFF baud
     * (3.8 baud at FCY = 4 MHz), so they are not worth carrying here. */
    U1MODEHbits.BCLKSEL = 0b00;     /* baud clock = FOSC/2 = FCY */
    U1MODEHbits.BCLKMOD = 1;        /* fractional baud generator */
    U1MODEbits.BRGH = 0;            /* must be 0 in fractional mode */

    if (baud == 0UL) {
        baud = 9600UL;              /* don't divide by zero on Serial.begin(0) */
    }
    {
        unsigned long brg = (FCY + (baud / 2UL)) / baud;   /* round to nearest */

        if (brg < 1UL) {
            brg = 1UL;
        } else if (brg > 0xFFFFFUL) {
            brg = 0xFFFFFUL;        /* clamp to the 20-bit field */
        }
        U1BRG  = (uint16_t)(brg & 0xFFFFUL);
        U1BRGH = (uint16_t)((brg >> 16) & 0x000FUL);
    }

    U1MODEbits.MOD = 0b0000;
    U1MODEHbits.STSEL = 0b00;

    _rx_buffer.head = 0;
    _rx_buffer.tail = 0;
    IPC2bits.U1RXIP = 3;
    IFS0bits.U1RXIF = 0;
    IEC0bits.U1RXIE = 1;

    /* Enable order matters: UARTEN must go high BEFORE UTXEN/URXEN. On this
     * UART module the transmitter and receiver enables are gated by UARTEN, so
     * setting them while the module is still off does not stick -- the result
     * is a UART that looks fully configured and never transmits a single bit.
     * This is the order Microchip's generated driver uses for these parts. */
    U1MODEbits.UARTEN = 1;
    U1MODEbits.UTXEN = 1;
    U1MODEbits.URXEN = 1;
}

void serial_end(void)
{
    while (!U1STAbits.TRMT);
    IEC0bits.U1RXIE = 0;
    U1MODEbits.UARTEN = 0;
    _rx_buffer.head = 0;
    _rx_buffer.tail = 0;
}

int serial_available(void)
{
    return (int)(((unsigned int)(SERIAL_BUFFER_SIZE + _rx_buffer.head - _rx_buffer.tail)) % SERIAL_BUFFER_SIZE);
}

int serial_read(void)
{
    if (_rx_buffer.head == _rx_buffer.tail) {
        return -1;
    }
    uint8_t c = _rx_buffer.buffer[_rx_buffer.tail];
    _rx_buffer.tail = (_rx_buffer.tail + 1) % SERIAL_BUFFER_SIZE;
    return (int)c;
}

int serial_peek(void)
{
    if (_rx_buffer.head == _rx_buffer.tail) {
        return -1;
    }
    return (int)_rx_buffer.buffer[_rx_buffer.tail];
}

void serial_flush(void)
{
    while (!U1STAbits.TRMT);
}

size_t serial_write(uint8_t c)
{
    while (U1STAHbits.UTXBF);
    U1TXREG = c;
    return 1;
}

size_t serial_print(const char *str)
{
    size_t n = 0;
    if (str == NULL) {
        return 0;
    }
    while (*str) {
        serial_write((uint8_t)*str++);
        n++;
    }
    return n;
}

size_t serial_println(const char *str)
{
    size_t n = serial_print(str);
    serial_write('\r');
    serial_write('\n');
    return n + 2;
}

/* Unsigned digit conversion, shared by both integer entry points. Split out so
 * that unsigned values above LONG_MAX print correctly -- Serial.println(millis())
 * is the common case, and millis() exceeds 2^31 after 24.8 days. */
size_t serial_print_uint(unsigned long uval, int base)
{
    char buf[34];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';

    /* base 0 would divide by zero and base 1 would never terminate. */
    if (base < 2 || base > 36) {
        base = 10;
    }

    do {
        unsigned int digit = (unsigned int)(uval % (unsigned long)base);
        *--p = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
        uval /= (unsigned long)base;
    } while (uval > 0);

    return serial_print(p);
}

size_t serial_print_int(long val, int base)
{
    /* Sign is base-10 only, matching standard Arduino: print(-1, HEX) shows the
     * two's-complement pattern rather than "-1". */
    if (val < 0 && base == 10) {
        serial_write('-');
        /* 0 - (unsigned)val rather than -val: negating LONG_MIN is undefined,
         * but the unsigned subtraction yields its magnitude correctly. */
        return 1 + serial_print_uint(0UL - (unsigned long)val, 10);
    }
    return serial_print_uint((unsigned long)val, base);
}

size_t serial_println_int(long val, int base)
{
    size_t n = serial_print_int(val, base);
    serial_write('\r');
    serial_write('\n');
    return n + 2;
}

size_t serial_println_uint(unsigned long val, int base)
{
    size_t n = serial_print_uint(val, base);
    serial_write('\r');
    serial_write('\n');
    return n + 2;
}

size_t serial_print_float(double val, int decimals)
{
    char buf[32];
    char *p = buf;
    size_t n = 0;

    /* buf holds sign + up to 10 integer digits + '.' + decimals + NUL. Clamp so
     * a large "decimals" cannot run off the end of the buffer. */
    if (decimals < 0) {
        decimals = 0;
    } else if (decimals > 16) {
        decimals = 16;
    }

    if (val < 0.0) {
        *p++ = '-';
        val = -val;
    }

    unsigned long int_part = (unsigned long)val;
    double remainder = val - (double)int_part;

    /* Integer part */
    char tmp[12];
    char *tp = tmp + sizeof(tmp) - 1;
    *tp = '\0';
    if (int_part == 0) {
        *--tp = '0';
    } else {
        while (int_part > 0) {
            *--tp = '0' + (int_part % 10);
            int_part /= 10;
        }
    }
    while (*tp) *p++ = *tp++;

    /* Decimal part */
    if (decimals > 0) {
        *p++ = '.';
        int i;
        for (i = 0; i < decimals; i++) {
            remainder *= 10.0;
            int digit = (int)remainder;
            *p++ = '0' + digit;
            remainder -= digit;
        }
    }
    *p = '\0';

    n = serial_print(buf);
    return n;
}

size_t serial_println_float(double val, int decimals)
{
    size_t n = serial_print_float(val, decimals);
    serial_write('\r');
    serial_write('\n');
    return n + 2;
}

#ifdef __cplusplus
}   /* extern "C" */
#endif

/* ============================================================
 * The global "Serial" object, in whichever form the language allows.
 * ============================================================ */
#ifndef __cplusplus

/* C: a struct of function pointers, which is what makes dot-notation work. */
HardwareSerial_t Serial = {
    .begin       = serial_begin,
    .end         = serial_end,
    .available   = serial_available,
    .read        = serial_read,
    .peek        = serial_peek,
    .flush       = serial_flush,
    .write       = serial_write,
    .print       = serial_print,
    .println     = serial_println,
    .print_int   = serial_print_int,
    .println_int = serial_println_int,
    .print_float = serial_print_float,
    .println_float = serial_println_float,
};

#else

/* C++: a stateless class whose inline methods call the same functions directly.
 * Defined outside the extern "C" block above so its language linkage matches
 * the declaration in HardwareSerial.h. One byte of .bss, no vtable. */
HardwareSerial Serial;

#endif
