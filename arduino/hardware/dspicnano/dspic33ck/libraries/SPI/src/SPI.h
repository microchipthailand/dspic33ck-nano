/*
 * SPI.h - SPI library for dsPIC33CK Arduino core
 *
 * Provides Arduino-style SPI object using struct + function pointers.
 * Usage:
 *   SPI.begin();
 *   SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
 *   uint8_t rx = SPI.transfer(0x55);
 *   SPI.endTransaction();
 *   SPI.end();
 */

#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <xc.h>     /* must come first — see the SPI name clash note below */

#ifdef __cplusplus
extern "C" {
#endif

/* SPI clock divider values (FCY / divider = SPI clock) */
#define SPI_CLOCK_DIV2      2
#define SPI_CLOCK_DIV4      4
#define SPI_CLOCK_DIV8      8
#define SPI_CLOCK_DIV16     16
#define SPI_CLOCK_DIV32     32
#define SPI_CLOCK_DIV64     64
#define SPI_CLOCK_DIV128    128

/* SPI modes (CPOL | CPHA) */
#define SPI_MODE0   0x00    /* CPOL=0, CPHA=0 */
#define SPI_MODE1   0x01    /* CPOL=0, CPHA=1 */
#define SPI_MODE2   0x02    /* CPOL=1, CPHA=0 */
#define SPI_MODE3   0x03    /* CPOL=1, CPHA=1 */

typedef struct {
    uint32_t clock;
    uint8_t  bitOrder;
    uint8_t  dataMode;
} SPISettings_t;

typedef struct {
    void    (*begin)(void);
    void    (*end)(void);
    void    (*beginTransaction)(SPISettings_t settings);
    void    (*endTransaction)(void);
    uint8_t (*transfer)(uint8_t data);
    uint16_t (*transfer16)(uint16_t data);
    void    (*setBitOrder)(uint8_t bitOrder);
    void    (*setClockDivider)(uint8_t divider);
    void    (*setDataMode)(uint8_t mode);
} SPIClass_t;

/*
 * Name clash with the device headers: every p33CK*.h declares
 *   typedef struct tagSPI { ... } SPI, *PSPI;
 * to give the SPI1/SPI2 SFR blocks a type, so the bare name `SPI` is already
 * taken at file scope and a typedef cannot be #undef'd. Redirect the name
 * instead — the object is really called ArduinoSPI, and sketches keep
 * writing SPI.begin() / SPI.transfer() unchanged.
 */
#define SPI ArduinoSPI

extern SPIClass_t SPI;

/* Helper to construct SPISettings */
static inline SPISettings_t SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) {
    SPISettings_t s;
    s.clock = clock;
    s.bitOrder = bitOrder;
    s.dataMode = dataMode;
    return s;
}

#ifdef __cplusplus
}
#endif

#endif /* SPI_H */
