/*
 * bl_nvm.c - flash erase / program / read for dsPIC33CK, via compiler builtins.
 *
 * Register-level provenance, because this is the one part of the bootloader that
 * can damage the device if it is wrong:
 *
 *   NVMCON, dsPIC33CK256MP508 Family Data Sheet DS70005349K, Register 5-1 -
 *   bit 15 WR (self-clearing), bit 14 WREN, bit 13 WRERR, NVMOP in bits 3:0
 *   with 0001 = memory double-word operation and 0011 = memory page erase.
 *   The unlock sequence is 0x55 then 0xAA into NVMKEY followed by setting WR,
 *   which is exactly what __builtin_write_NVM() emits:
 *
 *       mov #0x55,w0 / mov w0,_NVMKEY / mov #0xAA,w0 / mov w0,_NVMKEY
 *       bset _NVMCON,#15 / nop / nop
 *
 * Note what is deliberately NOT here: a row write. The 128-word row operation
 * needs the NVMSRCADR pointer pair and an op code that could not be sourced
 * from anything on this machine, only from MCC's per-device properties. Guessing
 * an NVM op code is not an acceptable risk, so programming is done with
 * double-word operations, whose encoding is documented above. A 128-word block
 * therefore costs 64 NVM operations instead of one - about 70 ms per block
 * instead of 1.1 ms. Uploads are still seconds, not minutes, and switching to a
 * row write later is a local change behind bl_nvm_write_dword().
 */

#include <xc.h>
#include "bl_config.h"
#include "bl_nvm.h"

/*
 * Arms the controller and runs whatever is already loaded in NVMCON.
 *
 * Interrupts are masked across the unlock sequence with DISI. The bootloader
 * never enables an interrupt source, so nothing should be able to land in the
 * middle of it - but GIE is set by default after reset, the IVT slots point at
 * an application region that may be mid-erase, and the cost of being defensive
 * here is two instructions.
 */
static int nvm_trigger(void)
{
    __builtin_disi(7);          /* covers the 7 instructions below */
    __builtin_write_NVM();      /* NVMKEY 0x55/0xAA, then WR = 1 */

    while (NVMCONbits.WR) {
        /* ~1.1 ms per operation; the hardware, not a delay loop, sets the pace */
    }

    return NVMCONbits.WRERR ? 0 : 1;
}

int bl_nvm_erase_page(uint32_t addr)
{
    if ((addr & (BL_ERASE_STEP - 1UL)) != 0UL) {
        return 0;
    }

    while (NVMCONbits.WR) {
        /* never stack an operation on a busy controller */
    }

    NVMADRU = (uint16_t)(addr >> 16);
    NVMADR  = (uint16_t)(addr & 0xFFFFUL);
    NVMCON  = BL_NVMOP_ERASE_PAGE;

    return nvm_trigger();
}

int bl_nvm_write_dword(uint32_t addr, uint32_t word0, uint32_t word1)
{
    uint16_t saved_tblpag;
    int ok;

    if ((addr & 3UL) != 0UL) {
        return 0;
    }

    while (NVMCONbits.WR) {
    }

    NVMADRU = (uint16_t)(addr >> 16);
    NVMADR  = (uint16_t)(addr & 0xFFFFUL);
    NVMCON  = BL_NVMOP_WRITE_DWORD;

    /* The two write latches are mapped through table page 0xFA at offsets 0 and
     * 2. TBLPAG is saved and restored because the compiler uses it for PSV
     * access to const data. */
    saved_tblpag = TBLPAG;
    TBLPAG = BL_NVM_LATCH_PAGE;
    __builtin_tblwtl(0, (uint16_t)(word0 & 0xFFFFUL));
    __builtin_tblwth(0, (uint16_t)((word0 >> 16) & 0x00FFUL));
    __builtin_tblwtl(2, (uint16_t)(word1 & 0xFFFFUL));
    __builtin_tblwth(2, (uint16_t)((word1 >> 16) & 0x00FFUL));

    ok = nvm_trigger();
    TBLPAG = saved_tblpag;

    return ok;
}

uint32_t bl_nvm_read_word(uint32_t addr)
{
    uint16_t saved_tblpag = TBLPAG;
    uint16_t lo, hi;

    TBLPAG = (uint16_t)(addr >> 16);
    lo = __builtin_tblrdl((uint16_t)(addr & 0xFFFFUL));
    hi = __builtin_tblrdh((uint16_t)(addr & 0xFFFFUL));
    TBLPAG = saved_tblpag;

    return ((uint32_t)(hi & 0x00FFu) << 16) | (uint32_t)lo;
}
