/*
 * bl_nvm.h - flash erase / program / read primitives for dsPIC33CK.
 */

#ifndef BL_NVM_H
#define BL_NVM_H

#include <stdint.h>

/* Erases one page. `addr` must be a multiple of BL_ERASE_STEP; a misaligned
 * address is rejected rather than masked, so a protocol bug cannot erase a page
 * the host did not name. Returns 1 on success, 0 on misalignment or WRERR. */
int bl_nvm_erase_page(uint32_t addr);

/* Programs two instruction words. `addr` must be 4-address aligned (one
 * double-word); only the low 24 bits of each value are used. Returns 1 on
 * success, 0 on misalignment or WRERR. */
int bl_nvm_write_dword(uint32_t addr, uint32_t word0, uint32_t word1);

/* Reads one instruction word from anywhere in program space. */
uint32_t bl_nvm_read_word(uint32_t addr);

#endif /* BL_NVM_H */
