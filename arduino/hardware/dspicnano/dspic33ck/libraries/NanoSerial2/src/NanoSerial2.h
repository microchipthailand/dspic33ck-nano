/*
 * NanoSerial2.h - UART2 for the dsPIC33CK Nano board
 *
 *   #include <NanoSerial2.h>
 *   Serial2.begin(9600);                         // default pins RX=RB10 TX=RB11
 *   Serial2.begin(9600, rxPin, txPin);           // any remappable pins
 *   Serial2.begin(9600, rxPin, txPin, dePin);    // RS485: dePin HIGH while sending
 *
 * rxPin / txPin must be remappable: any PORTB, PORTC or PORTD pin, NOT RA0-RA4.
 * Defaults used by the labs: RX = RB10 (D15, J2-4), TX = RB11 (D16, J2-5),
 * DE = RB12 (D17, J2-6).
 *
 * Same print()/println() style as Serial, so sketches read the same.
 */
#ifndef NANOSERIAL2_H
#define NANOSERIAL2_H

#include <Arduino.h>

#define SERIAL2_RX_BUFFER   128
#define SERIAL2_DEFAULT_RX  15   /* RB10, J2-4 */
#define SERIAL2_DEFAULT_TX  16   /* RB11, J2-5 */
#define SERIAL2_DEFAULT_DE  17   /* RB12, J2-6 */
#define SERIAL2_NO_DE       0xFF

#ifdef __cplusplus
extern "C" {
#endif
uint8_t serial2_begin(unsigned long baud, uint8_t rx, uint8_t tx, uint8_t de);
void    serial2_end(void);
int     serial2_available(void);
int     serial2_read(void);
int     serial2_peek(void);
void    serial2_flush(void);          /* waits until the last stop bit has left the pin */
size_t  serial2_write(uint8_t c);
unsigned long serial2_overruns(void); /* bytes dropped because the buffer was full */
#ifdef __cplusplus
}

class NanoSerial2 {
public:
    /* returns false if a pin cannot be remapped */
    bool begin(unsigned long baud,
               uint8_t rx = SERIAL2_DEFAULT_RX, uint8_t tx = SERIAL2_DEFAULT_TX,
               uint8_t de = SERIAL2_NO_DE)        { return serial2_begin(baud, rx, tx, de) != 0; }
    void end(void)                                { serial2_end(); }
    int  available(void)                          { return serial2_available(); }
    int  read(void)                               { return serial2_read(); }
    int  peek(void)                               { return serial2_peek(); }
    void flush(void)                              { serial2_flush(); }
    unsigned long overruns(void)                  { return serial2_overruns(); }

    size_t write(uint8_t c)                       { return serial2_write(c); }
    size_t write(const uint8_t *b, size_t n)      { size_t i; for (i = 0; i < n; i++) serial2_write(b[i]); return n; }
    size_t print(const char *s)                   { size_t n = 0; while (*s) n += serial2_write((uint8_t)*s++); return n; }
    size_t print(char c)                          { return serial2_write((uint8_t)c); }
    size_t print(long v, int base = DEC);
    size_t print(unsigned long v, int base = DEC);
    size_t print(int v, int base = DEC)           { return print((long)v, base); }
    size_t print(unsigned int v, int base = DEC)  { return print((unsigned long)v, base); }
    size_t println(void)                          { serial2_write('\r'); serial2_write('\n'); return 2; }
    size_t println(const char *s)                 { size_t n = print(s); return n + println(); }
    size_t println(char c)                        { size_t n = print(c); return n + println(); }
    size_t println(long v, int base = DEC)        { size_t n = print(v, base); return n + println(); }
    size_t println(unsigned long v, int base = DEC) { size_t n = print(v, base); return n + println(); }
    size_t println(int v, int base = DEC)         { size_t n = print(v, base); return n + println(); }
    size_t println(unsigned int v, int base = DEC){ size_t n = print(v, base); return n + println(); }
};

extern NanoSerial2 Serial2;
#endif /* __cplusplus */

#endif
