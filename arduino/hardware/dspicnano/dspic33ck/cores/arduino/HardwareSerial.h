/*
 * HardwareSerial.h - UART Serial for dsPIC33CK Arduino core
 *
 * Sketches use the standard Arduino spelling, with real overloads:
 *   Serial.begin(115200);
 *   Serial.println("Hello");
 *   Serial.println(analogRead(A0));
 *   Serial.print(value, HEX);
 *   Serial.println(1.5, 2);
 *   Serial.println();                        - bare CRLF
 *
 * The suffixed forms (print_int, println_int, print_float, println_float) are
 * still present and unchanged, for sketches written against earlier versions of
 * this core.
 *
 * This file is compiled both as C++ (by the Arduino IDE, which builds the whole
 * core as C++) and as C (by the MPLAB X projects under cmake/). In C there are
 * no overloads, so "Serial" is a struct of function pointers and only the
 * suffixed numeric forms exist. Both faces call the same serial_* functions in
 * HardwareSerial.c.
 */

#ifndef HARDWARE_SERIAL_H
#define HARDWARE_SERIAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERIAL_BUFFER_SIZE 64

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

typedef struct {
    uint8_t buffer[SERIAL_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} ring_buffer_t;

/* The implementation, in HardwareSerial.c. */
void   serial_begin(unsigned long baud);
void   serial_end(void);
int    serial_available(void);
int    serial_read(void);
int    serial_peek(void);
void   serial_flush(void);
size_t serial_write(uint8_t c);
size_t serial_print(const char *str);
size_t serial_println(const char *str);
size_t serial_print_int(long val, int base);
size_t serial_println_int(long val, int base);
size_t serial_print_uint(unsigned long val, int base);
size_t serial_println_uint(unsigned long val, int base);
size_t serial_print_float(double val, int decimals);
size_t serial_println_float(double val, int decimals);

#ifndef __cplusplus
/* C only: dot-notation via function pointers. C++ gets the class below instead,
 * because a class cannot have both a data member and a method called "print". */
typedef struct {
    void   (*begin)(unsigned long baud);
    void   (*end)(void);
    int    (*available)(void);
    int    (*read)(void);
    int    (*peek)(void);
    void   (*flush)(void);
    size_t (*write)(uint8_t c);
    size_t (*print)(const char *str);
    size_t (*println)(const char *str);
    size_t (*print_int)(long val, int base);
    size_t (*println_int)(long val, int base);
    size_t (*print_float)(double val, int decimals);
    size_t (*println_float)(double val, int decimals);
} HardwareSerial_t;

extern HardwareSerial_t Serial;
#endif /* !__cplusplus */

#ifdef __cplusplus
}   /* extern "C" */
#endif

#ifdef __cplusplus

/*
 * Stateless wrapper giving the standard Arduino overload set. Every method is
 * inline and forwards straight to the corresponding serial_* function, so this
 * costs no code beyond the call that C would have made through the struct, and
 * one byte of .bss instead of the struct's 26 bytes of .data.
 *
 * Deliberately no overload for unsigned char / bool / short: those undergo
 * integral promotion to int, which is a better match than any conversion, so
 * they resolve to print(int, int) with no ambiguity -- and print as numbers,
 * exactly as standard Arduino does.
 */
class HardwareSerial {
public:
    void begin(unsigned long baud) { serial_begin(baud); }
    void end(void)                 { serial_end(); }
    int  available(void)           { return serial_available(); }
    int  read(void)                { return serial_read(); }
    int  peek(void)                { return serial_peek(); }
    void flush(void)               { serial_flush(); }

    size_t write(uint8_t c)        { return serial_write(c); }
    size_t write(const char *str)  { return serial_print(str); }
    size_t write(const uint8_t *buf, size_t n)
    {
        size_t i;
        for (i = 0; i < n; i++) {
            serial_write(buf[i]);
        }
        return n;
    }

    /* ---- print ---- */
    size_t print(const char *str)  { return serial_print(str); }
    size_t print(char c)           { return serial_write((uint8_t)c); }

    size_t print(int val, int base = DEC)
        { return serial_print_int((long)val, base); }
    size_t print(unsigned int val, int base = DEC)
        { return serial_print_uint((unsigned long)val, base); }
    size_t print(long val, int base = DEC)
        { return serial_print_int(val, base); }
    size_t print(unsigned long val, int base = DEC)
        { return serial_print_uint(val, base); }
    size_t print(double val, int decimals = 2)
        { return serial_print_float(val, decimals); }

    /* ---- println ---- */
    size_t println(void)             { serial_write('\r'); serial_write('\n'); return 2; }
    size_t println(const char *str)  { return serial_println(str); }
    size_t println(char c)
        { serial_write((uint8_t)c); serial_write('\r'); serial_write('\n'); return 3; }

    size_t println(int val, int base = DEC)
        { return serial_println_int((long)val, base); }
    size_t println(unsigned int val, int base = DEC)
        { return serial_println_uint((unsigned long)val, base); }
    size_t println(long val, int base = DEC)
        { return serial_println_int(val, base); }
    size_t println(unsigned long val, int base = DEC)
        { return serial_println_uint(val, base); }
    size_t println(double val, int decimals = 2)
        { return serial_println_float(val, decimals); }

    /* ---- retained from the pre-overload API ---- */
    size_t print_int(long val, int base)          { return serial_print_int(val, base); }
    size_t println_int(long val, int base)        { return serial_println_int(val, base); }
    size_t print_float(double val, int decimals)  { return serial_print_float(val, decimals); }
    size_t println_float(double val, int decimals){ return serial_println_float(val, decimals); }

    /* "if (Serial)" -- this UART needs no host enumeration, so always ready. */
    operator bool(void) const { return true; }
};

extern HardwareSerial Serial;

#endif /* __cplusplus */

#endif /* HARDWARE_SERIAL_H */
