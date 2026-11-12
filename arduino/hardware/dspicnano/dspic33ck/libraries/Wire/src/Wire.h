/*
 * Wire.h - I2C library for dsPIC33CK Arduino core
 *
 * Provides Arduino-style Wire object using struct + function pointers.
 * Usage:
 *   Wire.begin();
 *   Wire.beginTransmission(0x50);
 *   Wire.write(0x00);
 *   Wire.write(0xAB);
 *   Wire.endTransmission();
 *
 *   Wire.requestFrom(0x50, 2);
 *   while (Wire.available()) {
 *       uint8_t b = Wire.read();
 *   }
 */

#ifndef WIRE_H
#define WIRE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRE_BUFFER_SIZE 32

typedef struct {
    void    (*begin)(void);
    void    (*end)(void);
    void    (*setClock)(uint32_t clock);
    void    (*beginTransmission)(uint8_t address);
    uint8_t (*endTransmission)(void);
    uint8_t (*endTransmissionStop)(uint8_t sendStop);
    uint8_t (*requestFrom)(uint8_t address, uint8_t quantity);
    uint8_t (*requestFromStop)(uint8_t address, uint8_t quantity, uint8_t sendStop);
    size_t  (*write)(uint8_t data);
    size_t  (*writeBytes)(const uint8_t *data, size_t length);
    int     (*available)(void);
    int     (*read)(void);
    int     (*peek)(void);
} WireClass_t;

extern WireClass_t Wire;

#ifdef __cplusplus
}
#endif

#endif /* WIRE_H */
