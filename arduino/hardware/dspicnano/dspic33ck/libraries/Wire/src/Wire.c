/*
 * Wire.c - I2C1 implementation for dsPIC33CK Arduino core
 *
 * Uses I2C1 module on dedicated SDA1/SCL1 pins (no PPS needed).
 * Master mode only. Polled (no interrupts).
 *
 * Pin direction is set from variant-defined macros:
 *   WIRE_SDA_TRIS, WIRE_SCL_TRIS
 *
 * Default clock: 100 kHz (standard mode).
 */

#include "Arduino.h"
#include "Wire.h"

#ifdef __cplusplus
extern "C" {
#endif

static uint8_t _tx_buffer[WIRE_BUFFER_SIZE];
static uint8_t _tx_length;
static uint8_t _tx_address;

static uint8_t _rx_buffer[WIRE_BUFFER_SIZE];
static uint8_t _rx_length;
static uint8_t _rx_index;

static void _wire_begin(void)
{
    /* SDA/SCL are open-drain on dsPIC33CK I2C dedicated pins.
       Set as inputs (peripheral takes control). */
    WIRE_SDA_TRIS = 1;
    WIRE_SCL_TRIS = 1;

    /* Disable I2C1 for configuration */
    I2C1CONLbits.I2CEN = 0;

    /* 100 kHz standard mode: BRG = (FCY / (2 * Fscl)) - 1 */
    I2C1BRG = (FCY / (2UL * 100000UL)) - 1;

    /* Standard settings */
    I2C1CONL = 0x0000;
    I2C1CONH = 0x0000;

    /* Enable I2C1 */
    I2C1CONLbits.I2CEN = 1;

    _tx_length = 0;
    _rx_length = 0;
    _rx_index = 0;
}

static void _wire_end(void)
{
    I2C1CONLbits.I2CEN = 0;
}

static void _wire_setClock(uint32_t clock)
{
    I2C1CONLbits.I2CEN = 0;
    I2C1BRG = (FCY / (2UL * clock)) - 1;
    I2C1CONLbits.I2CEN = 1;
}

static void _wait_idle(void)
{
    while (I2C1CONLbits.SEN || I2C1CONLbits.RSEN ||
           I2C1CONLbits.PEN || I2C1CONLbits.RCEN ||
           I2C1CONLbits.ACKEN || I2C1STATbits.TRSTAT);
}

static void _wire_beginTransmission(uint8_t address)
{
    _tx_address = address;
    _tx_length = 0;
}

static uint8_t _wire_endTransmissionStop(uint8_t sendStop)
{
    uint8_t i;

    _wait_idle();

    /* Start condition */
    I2C1CONLbits.SEN = 1;
    while (I2C1CONLbits.SEN);

    /* Send address + write bit */
    I2C1TRN = (_tx_address << 1) | 0;
    while (I2C1STATbits.TRSTAT);
    if (I2C1STATbits.ACKSTAT) {
        /* NACK received — device not responding */
        I2C1CONLbits.PEN = 1;
        while (I2C1CONLbits.PEN);
        return 2;   /* Arduino Wire: 2 = NACK on address */
    }

    /* Send data bytes */
    for (i = 0; i < _tx_length; i++) {
        I2C1TRN = _tx_buffer[i];
        while (I2C1STATbits.TRSTAT);
        if (I2C1STATbits.ACKSTAT) {
            I2C1CONLbits.PEN = 1;
            while (I2C1CONLbits.PEN);
            return 3;   /* Arduino Wire: 3 = NACK on data */
        }
    }

    /* Stop condition (or repeated start will follow) */
    if (sendStop) {
        I2C1CONLbits.PEN = 1;
        while (I2C1CONLbits.PEN);
    }

    return 0;   /* Success */
}

static uint8_t _wire_endTransmission(void)
{
    return _wire_endTransmissionStop(1);
}

static uint8_t _wire_requestFromStop(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
    uint8_t i;

    if (quantity > WIRE_BUFFER_SIZE) {
        quantity = WIRE_BUFFER_SIZE;
    }

    _wait_idle();

    /* Start condition */
    I2C1CONLbits.SEN = 1;
    while (I2C1CONLbits.SEN);

    /* Send address + read bit */
    I2C1TRN = (address << 1) | 1;
    while (I2C1STATbits.TRSTAT);
    if (I2C1STATbits.ACKSTAT) {
        I2C1CONLbits.PEN = 1;
        while (I2C1CONLbits.PEN);
        _rx_length = 0;
        _rx_index = 0;
        return 0;
    }

    /* Receive bytes */
    for (i = 0; i < quantity; i++) {
        _wait_idle();
        I2C1CONLbits.RCEN = 1;
        while (I2C1CONLbits.RCEN);
        _rx_buffer[i] = (uint8_t)I2C1RCV;

        /* ACK for all bytes except last */
        _wait_idle();
        if (i < (quantity - 1)) {
            I2C1CONLbits.ACKDT = 0; /* ACK */
        } else {
            I2C1CONLbits.ACKDT = 1; /* NACK last byte */
        }
        I2C1CONLbits.ACKEN = 1;
        while (I2C1CONLbits.ACKEN);
    }

    /* Stop condition */
    if (sendStop) {
        I2C1CONLbits.PEN = 1;
        while (I2C1CONLbits.PEN);
    }

    _rx_length = quantity;
    _rx_index = 0;
    return quantity;
}

static uint8_t _wire_requestFrom(uint8_t address, uint8_t quantity)
{
    return _wire_requestFromStop(address, quantity, 1);
}

static size_t _wire_write(uint8_t data)
{
    if (_tx_length >= WIRE_BUFFER_SIZE) {
        return 0;
    }
    _tx_buffer[_tx_length++] = data;
    return 1;
}

static size_t _wire_writeBytes(const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++) {
        if (_wire_write(data[i]) == 0) break;
    }
    return i;
}

static int _wire_available(void)
{
    return (int)(_rx_length - _rx_index);
}

static int _wire_read(void)
{
    if (_rx_index >= _rx_length) {
        return -1;
    }
    return (int)_rx_buffer[_rx_index++];
}

static int _wire_peek(void)
{
    if (_rx_index >= _rx_length) {
        return -1;
    }
    return (int)_rx_buffer[_rx_index];
}

/*
 * Global Wire object.
 *
 * Positional, not designated: platform.txt sets compiler.c.cmd to the g++ driver,
 * and g++ compiles a .c file as C++, where designated initializers are a GNU
 * extension that -Wpedantic reports. The order below must stay in step with
 * WireClass_t in Wire.h -- hence the trailing names.
 */
WireClass_t Wire = {
    _wire_begin,              /* begin               */
    _wire_end,                /* end                 */
    _wire_setClock,           /* setClock            */
    _wire_beginTransmission,  /* beginTransmission   */
    _wire_endTransmission,    /* endTransmission     */
    _wire_endTransmissionStop,/* endTransmissionStop */
    _wire_requestFrom,        /* requestFrom         */
    _wire_requestFromStop,    /* requestFromStop     */
    _wire_write,              /* write               */
    _wire_writeBytes,         /* writeBytes          */
    _wire_available,          /* available           */
    _wire_read,               /* read                */
    _wire_peek                /* peek                */
};

#ifdef __cplusplus
}
#endif
