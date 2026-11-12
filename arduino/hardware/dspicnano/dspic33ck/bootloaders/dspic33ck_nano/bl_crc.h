/*
 * bl_crc.h - the two checks on the wire and on the image.
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final xor)
 * guards every frame. CRC-32 (the IEEE/zip one: poly 0xEDB88320 reflected,
 * init 0xFFFFFFFF, final xor 0xFFFFFFFF) verifies the programmed image.
 *
 * The image CRC is defined over BYTES OF INSTRUCTION WORDS, low word first,
 * three bytes per word, little-endian within the word, phantom byte excluded.
 * The host must build exactly the same byte stream - including filling any gap
 * inside the image span with 0xFFFFFF, which is what erased flash reads as.
 * tools/serial_upload.py carries the same definition in its own comment.
 */

#ifndef BL_CRC_H
#define BL_CRC_H

#include <stdint.h>

uint16_t bl_crc16(const uint8_t *buf, uint16_t len);

/* CRC-32 over `len` address units of program flash starting at `addr`. */
uint32_t bl_crc32_flash(uint32_t addr, uint32_t len);

#endif /* BL_CRC_H */
