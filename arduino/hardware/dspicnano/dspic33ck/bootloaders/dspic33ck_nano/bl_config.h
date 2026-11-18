/*
 * bl_config.h - memory map, flash geometry and wire protocol for the
 *               dsPIC33CK256MC005 serial bootloader.
 *
 * Everything the bootloader and the host must agree on lives here, and the
 * addresses are the same numbers the linker scripts use. They are NOT restated
 * from memory: tools/gld/gen_bootloader_gld.py generates
 * ldscripts/p33CK256MC005-{boot,app}.gld from the device's stock .gld, and
 * _build/bootloader_check.sh asserts that the three constants below match the
 * generated application script. If you change one, the gate fails.
 */

#ifndef BL_CONFIG_H
#define BL_CONFIG_H

/* --- identity ----------------------------------------------------------- */

#define BL_PROTO_VERSION    1u          /* bumped on any wire-format change */

/* --- memory map (program-space address units) ---------------------------- */
/*
 * One 24-bit instruction word occupies two address units, so every address
 * here is even and a word count is half an address count.
 *
 *   0x000000  reset vector   GOTO bootloader              ] owned by the
 *   0x000004  IVT, 254 slots constants -> app trampoline   ] bootloader,
 *   0x000200  bootloader code, capped at 0x1600            ] NEVER erased
 *   0x001800  app entry GOTO + 200-entry GOTO trampoline   ] erased and
 *   0x001B24  application code, contiguous                 ] rewritten on
 *   0x02B700  signature row  length / CRC-32 / magic       ] every upload
 *   0x02B800  unused - shares an erase page with the config words
 *   0x02BF00  config words   FOSCSEL / FOSC / FWDT / FICD
 */
#define BL_APP_BASE         0x001800UL  /* app entry point; the only jump target */
#define BL_SIG_BASE         0x02B700UL  /* signature row: last row of the app region */
#define BL_APP_END          0x02B800UL  /* first address the bootloader must NOT erase */

/*
 * Why the application stops at 0x02B800 and not at the end of the program
 * region (0x02BF00): the config words live at 0x02BF00, inside the erase page
 * that starts at 0x02B800. Erasing that page clears FOSCSEL/FOSC/FWDT/FICD,
 * and an erased FWDTEN reads as "watchdog enabled" - the board would then reset
 * itself in a loop with no way in but a debugger. So the last 0x700 addresses
 * of the program region are permanently unused. That is the price of erasing
 * the application with plain page erases and no special cases.
 */

/* --- flash geometry ------------------------------------------------------ */
/*
 * Erase step, deliberately conservative.
 *
 * The EDC description of this device says erasepagesize="1024" with
 * sizeunits="words", which reads either as 1024 instruction words (0x800
 * addresses) or as 1024 addresses (0x400, i.e. 512 instruction words) - and
 * Microchip's own field-proven code for this family disagrees with the wider
 * reading, using a 0x400 page. That cannot be settled from the files on this
 * machine, so the bootloader is built to be correct under BOTH readings:
 *
 *   - it ALIGNS and RESERVES with 0x800 (the larger), so no region boundary can
 *     ever land inside a page that is shared with something it must not erase;
 *   - it ERASES with a 0x400 step across the whole application region. If the
 *     page really is 0x800, each page is simply erased twice, which is harmless
 *     because every erase happens before any programming. If the page is 0x400
 *     and we stepped by 0x800, we would erase every OTHER page and then program
 *     into un-erased flash - the failure this choice exists to prevent.
 *
 * Microchip's EZBL bootloader library is set to a 0x400 erase step on both
 * dsPIC33CH and dsPIC33CK boards in shipped demos, which is the same
 * conservative choice: a 0x400 step is correct whichever the page turns out to
 * be, a 0x800 step is only correct if the page really is 0x800.
 *
 * Nothing else depends on the page size: programming uses double-word writes,
 * not row writes, so BL_WIRE_BLOCK_WORDS below is a wire chunk size and not a
 * hardware row.
 */
#define BL_ERASE_STEP       0x400UL     /* erase every 0x400 addresses */
#define BL_PAGE_RESERVE     0x800UL     /* alignment granularity for boundaries */
#define BL_WIRE_BLOCK_WORDS 128u        /* instruction words per WRITE_ROW frame */

/* NVM operation codes, NVMCON<15:0> with WREN (bit 14) already set.
 *
 * Source: dsPIC33CK256MP508 Family Data Sheet DS70005349K, Register 5-1,
 * NVMOP[3:0]: 0001 = memory double-word operation, 0011 = memory page erase.
 * Bit 15 = WR (self-clearing), bit 14 = WREN, bit 13 = WRERR. */
#define BL_NVMOP_WRITE_DWORD 0x4001u
#define BL_NVMOP_ERASE_PAGE  0x4003u

/* The write latches are mapped through table page 0xFA on this family. */
#define BL_NVM_LATCH_PAGE    0x00FAu

/* --- signature row ------------------------------------------------------- */
/*
 * Six instruction words at BL_SIG_BASE, written by COMMIT as three double-word
 * operations. The magic pair goes LAST and in one operation, so the signature
 * is either fully present or not valid at all: lose power mid-upload and the
 * bootloader simply refuses to jump and waits for the host again. That is what
 * makes an interrupted upload recoverable over serial alone, with no debugger.
 *
 *   +0x0  image length in address units (app_base .. app_base + length)
 *   +0x2  CRC-32, bits 23:0
 *   +0x4  CRC-32, bits 31:24
 *   +0x6  signature layout version
 *   +0x8  magic
 *   +0xA  ~magic, so an all-ones (erased) row cannot look valid
 */
#define BL_SIG_OFF_LENGTH   0x0u
#define BL_SIG_OFF_CRC_LO   0x2u
#define BL_SIG_OFF_CRC_HI   0x4u
#define BL_SIG_OFF_VERSION  0x6u
#define BL_SIG_OFF_MAGIC    0x8u
#define BL_SIG_OFF_NMAGIC   0xAu
#define BL_SIG_WORDS        6u

#define BL_SIG_MAGIC        0x4B4333UL  /* '3','C','K' little-endian */
#define BL_SIG_VERSION      0x000001UL

/* --- wire protocol ------------------------------------------------------- */
/*
 * Request   SOH CMD LEN_LO LEN_HI payload[LEN] CRC_LO CRC_HI
 * Response  STATUS  LEN_LO LEN_HI payload[LEN] CRC_LO CRC_HI
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over everything between the
 * first byte and the CRC itself - i.e. CMD/STATUS, both length bytes and the
 * payload. The response needs no SOH of its own: STATUS is the frame marker.
 */
#define BL_SOH              0x01u
#define BL_ACK              0x06u
#define BL_NAK              0x15u

#define BL_CMD_SYNC         0x10u   /* -> identity and geometry, never assumed */
#define BL_CMD_ERASE_APP    0x20u   /* -> erase the whole application region */
#define BL_CMD_WRITE_ROW    0x30u   /* addr32 + N*3 bytes, little-endian words */
#define BL_CMD_READ_CRC     0x40u   /* addr32 + len32 -> CRC-32 of that range */
#define BL_CMD_COMMIT       0x50u   /* len32 + crc32 -> write the signature row */
#define BL_CMD_JUMP         0x60u   /* validate the signature, then run the app */

#define BL_ERR_CRC          0x01u   /* frame CRC-16 mismatch */
#define BL_ERR_CMD          0x02u   /* unknown command */
#define BL_ERR_LEN          0x03u   /* payload length wrong for this command */
#define BL_ERR_RANGE        0x04u   /* address outside the application region */
#define BL_ERR_ALIGN        0x05u   /* address not double-word aligned */
#define BL_ERR_NVM          0x06u   /* WRERR: the erase or write did not take */
#define BL_ERR_NOAPP        0x07u   /* signature missing or CRC-32 mismatch */

/* 4 address bytes + 128 instruction words as 3 bytes each. */
#define BL_MAX_PAYLOAD      (4u + (BL_WIRE_BLOCK_WORDS * 3u))

/* --- entry -------------------------------------------------------------- */
/*
 * The window is short because it is paid at every reset of every sketch. The
 * host does not rely on it alone: it asks a running sketch to reset itself and
 * then floods SYNC frames, so the window only has to be wider than the host's
 * frame rate, not wide enough for a human.
 */
#define BL_WINDOW_MS        300u
#define BL_FRAME_GAP_MS     250u    /* inter-byte timeout inside one frame */

/*
 * Soft entry: the sequence the host sends to a RUNNING SKETCH to ask it to reset
 * itself into that window. The bootloader never looks at it - by the time the
 * bootloader runs the sketch is already gone - so this constant is here because
 * this file is where the protocol lives, and it is used by two other places:
 *
 *   cores/arduino/HardwareSerial.c   the RX interrupt matches it and resets
 *   tools/serial_upload.py           the host sends it
 *
 * Ten bytes: eight chosen to be improbable in ordinary traffic, followed by the
 * CRC-16/CCITT-FALSE of those eight. The CRC adds nothing an attacker would care
 * about - the whole sequence is a constant - but it does mean a garbled or
 * truncated prefix cannot reset a running sketch, which is the accident that
 * actually happens. _build/bootloader_check.sh asserts that the sketch-side copy
 * of this sequence is byte-identical to this one.
 */
#define BL_SOFT_ENTRY_MAGIC { 0x1Bu, 0xF0u, 0x33u, 0x43u, 0x4Bu, 0x21u, \
                              0x9Eu, 0x57u, 0xE8u, 0x3Bu }
#define BL_SOFT_ENTRY_LEN   10u

/* --- board -------------------------------------------------------------- */
/*
 * dsPIC33CK Nano (custom board). Same pins the core uses, from
 * variants/dspic33ck_nano/pins_arduino.h:
 *   USER LED = RC13, active low   (LED_BUILTIN, pin 34)
 *   USER SW2 = RC12, active low   (BUTTON_BUILTIN, pin 33), external 10k pull-up
 *   U1TX     = RC10 (RP58)        (PIN_SERIAL_TX, pin 31) -> header J5-2
 *   U1RX     = RC11 (RP59)        (PIN_SERIAL_RX, pin 32) <- header J5-3
 *
 * Manual entry: hold USER, press and release RESET, keep USER held ~1 s.
 *
 * RC12 has no analog function on this device (ANSELC covers RC0-RC3, RC6, RC7
 * only), so unlike RD13 on the EV08P02A there is no ANSEL bit to clear before
 * the button can be read. BL_SW0_ANSEL is deliberately left undefined; bl_main.c
 * only touches it when it exists. The internal pull-up is enabled anyway: it is
 * in parallel with R6 and costs nothing.
 */
#define BL_LED_LAT          LATCbits.LATC13
#define BL_LED_TRIS         TRISCbits.TRISC13
#define BL_SW0_PORT         PORTCbits.RC12
#define BL_SW0_TRIS         TRISCbits.TRISC12
#define BL_SW0_CNPU         CNPUCbits.CNPUC12

#define BL_TX_LAT           LATCbits.LATC10
#define BL_TX_TRIS          TRISCbits.TRISC10
#define BL_RX_TRIS          TRISCbits.TRISC11
#define BL_TX_RP_REG        _RP58R
#define BL_RX_RP_NUM        59u
#define BL_PPS_OUT_U1TX     1u

/* --- clock -------------------------------------------------------------- */
/*
 * The bootloader runs on the reset-default FRC and brings up no PLL: FNOSC is
 * FRC, FRCDIV defaults to /1, so FOSC = 8 MHz and FCY = 4 MHz. Every reset -
 * including the `reset` instruction a sketch executes for soft entry - reloads
 * the oscillator from FNOSC, so this is true even when the sketch was running
 * at 100 MIPS.
 *
 * 115200 baud is reachable from 4 MHz only through the fractional baud
 * generator (BCLKMOD = 1, baud = FCY / BRG): BRG = 35 gives 114286, -0.7%.
 * The classic /16 divisor would land on 125000 baud, 8.5% out, and would have
 * forced PLL initialisation into the bootloader.
 */
#define BL_FCY              4000000UL
#define BL_BAUD             115200UL

#endif /* BL_CONFIG_H */
