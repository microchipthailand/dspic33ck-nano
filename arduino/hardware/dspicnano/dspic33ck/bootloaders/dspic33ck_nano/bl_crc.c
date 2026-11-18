/*
 * bl_crc.c - CRC-16/CCITT-FALSE for frames, CRC-32 for the image.
 *
 * The CRC-32 uses a 16-entry nibble table rather than the usual 256-entry byte
 * table or a bit-at-a-time loop. The table costs 64 bytes of flash; the reason
 * is speed, and speed here is measured in whole seconds. At FCY = 4 MHz - the
 * bare FRC, no PLL - a bitwise CRC-32 over a full 256 KB image takes several
 * seconds, long enough that the host would have to be told to wait. The nibble
 * table brings a typical sketch (a few KB) down to tens of milliseconds.
 */

#include "bl_config.h"
#include "bl_crc.h"
#include "bl_nvm.h"

uint16_t bl_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (bit = 0; bit < 8u; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Reflected CRC-32, four bits at a time. */
static const uint32_t crc32_nibble[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
    crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
    return crc;
}

uint32_t bl_crc32_flash(uint32_t addr, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t a;

    for (a = addr; a < addr + len; a += 2UL) {
        uint32_t w = bl_nvm_read_word(a);

        crc = crc32_byte(crc, (uint8_t)(w & 0xFFUL));
        crc = crc32_byte(crc, (uint8_t)((w >> 8) & 0xFFUL));
        crc = crc32_byte(crc, (uint8_t)((w >> 16) & 0xFFUL));
    }

    return crc ^ 0xFFFFFFFFUL;
}
