/*
 * bl_main.c - entry decision, frame parser and command handlers.
 *
 * The bootloader is fully polled: it enables no interrupt source and needs no
 * vectors of its own, which is why every one of the 254 IVT slots it programs
 * can forward unconditionally into the application's trampoline. A trap taken
 * while the bootloader runs therefore lands in the application region - which
 * may be mid-erase - and hangs. That is a bug's symptom, not a failure mode to
 * recover from: the host times out and the board is still reprogrammable,
 * because nothing the bootloader writes can damage page 0.
 */

#include <xc.h>
#include <stdint.h>
#include "bl_config.h"
#include "bl_crc.h"
#include "bl_nvm.h"
#include "bl_time.h"
#include "bl_uart.h"

/* ============================================================
 * Configuration bits
 *
 * The bootloader is the image that OWNS the config words: it is programmed by
 * the debugger, and the sketches uploaded over serial never write them (the
 * whole point of stopping the application region below 0x02B800 is that their
 * erase page is out of reach). So these must be exactly the values
 * cores/arduino/system_config.c sets for this board - if they drift, every
 * sketch runs with config bits it did not ask for.
 *
 * _build/bootloader_check.sh compares the four config records this file emits
 * against the ones system_config.c emits, so a drift is a failed gate rather
 * than a field mystery.
 * ============================================================ */

#pragma config FNOSC = FRC          /* Fast RC oscillator */
#pragma config IESO = OFF           /* Two-speed start-up disabled */
#pragma config POSCMD = NONE        /* Primary oscillator disabled */
#pragma config FCKSM = CSECMD       /* Clock switching enabled, FSCM disabled */
#pragma config JTAGEN = OFF         /* JTAG disabled */
#pragma config FWDTEN = ON_SW       /* WDT controlled by software (off at startup) */
#pragma config OSCIOFNC = ON        /* OSC2 pin is digital I/O */
#pragma config RWDTPS = PS32768     /* WDT period */
#pragma config WINDIS = ON          /* WDT window disabled (standard mode) */
#pragma config ICS = PGD3           /* ICSP header J4 uses PGC3/PGD3 */

/* ============================================================
 * Frame buffers
 * ============================================================ */

/* [0] = CMD/STATUS, [1..2] = length, [3..] = payload. Keeping the header in the
 * same buffer as the payload means the CRC-16 is one call over one span. */
#define BL_MAX_RESP_PAYLOAD 32u

static uint8_t rx[3u + BL_MAX_PAYLOAD];
static uint8_t tx[3u + BL_MAX_RESP_PAYLOAD + 2u];

#define RX_CMD      (rx[0])
#define RX_PAYLOAD  (&rx[3])

static uint16_t rx_len;

typedef enum {
    RX_NONE = 0,        /* nothing arrived, or a stray byte: say nothing, listen again */
    RX_OK,
    RX_BAD              /* framed but corrupt, or longer than we can hold: NAK */
} rx_result_t;

/* ============================================================
 * Board
 * ============================================================ */

static void led_init(void)
{
    BL_LED_LAT  = 1;                /* active low: 1 = off */
    BL_LED_TRIS = 0;
}

static void led(int on)
{
    BL_LED_LAT = on ? 0 : 1;
}

static void led_release(void)
{
    BL_LED_LAT  = 1;
    BL_LED_TRIS = 1;                /* back to an input, as after a power-on reset */
}

static void sw0_init(void)
{
    /* Digital first. While ANSEL is set the input buffer is off and the pin reads
     * 0 regardless of the pull-up, which is indistinguishable from the button
     * being held - see BL_SW0_ANSEL in bl_config.h. */
#ifdef BL_SW0_ANSEL
    BL_SW0_ANSEL = 0;
#endif
    BL_SW0_TRIS = 1;
    BL_SW0_CNPU = 1;                /* the button pulls to ground, so pull up */
}

static void sw0_release(void)
{
    BL_SW0_CNPU = 0;
#ifdef BL_SW0_ANSEL
    BL_SW0_ANSEL = 1;               /* back to the power-on default */
#endif
}

static int sw0_pressed(void)
{
    /* The pull-up needs time to charge the pin before the first read means
     * anything. A few microseconds is plenty; a tick is 64 us. */
    uint16_t t0 = bl_ticks();

    while (bl_ticks_since(t0) < 2u) {
    }

    return (BL_SW0_PORT == 0);      /* active low */
}

/* ============================================================
 * Responses
 * ============================================================ */

static void respond(uint8_t status, uint16_t len)
{
    uint16_t crc;

    tx[0] = status;
    tx[1] = (uint8_t)(len & 0xFFu);
    tx[2] = (uint8_t)(len >> 8);
    crc = bl_crc16(tx, (uint16_t)(3u + len));
    tx[3u + len]      = (uint8_t)(crc & 0xFFu);
    tx[3u + len + 1u] = (uint8_t)(crc >> 8);

    bl_uart_write(tx, (uint16_t)(3u + len + 2u));
}

static void ack(void)
{
    respond(BL_ACK, 0);
}

static void nak(uint8_t err)
{
    tx[3] = err;
    respond(BL_NAK, 1);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFUL);
    p[1] = (uint8_t)((v >> 8) & 0xFFUL);
    p[2] = (uint8_t)((v >> 16) & 0xFFUL);
    p[3] = (uint8_t)((v >> 24) & 0xFFUL);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ============================================================
 * Frame parser
 * ============================================================ */

static rx_result_t rx_frame(uint16_t soh_timeout)
{
    uint16_t gap = BL_MS_TO_TICKS(BL_FRAME_GAP_MS);
    uint16_t i, crc_rx;
    uint8_t  b, lo, hi;

    if (!bl_uart_getc(soh_timeout, &b)) {
        return RX_NONE;
    }
    if (b != BL_SOH) {
        return RX_NONE;             /* resync on the next byte, silently */
    }

    for (i = 0; i < 3u; i++) {
        if (!bl_uart_getc(gap, &rx[i])) {
            return RX_NONE;         /* the sender vanished mid-frame */
        }
    }
    rx_len = (uint16_t)rx[1] | ((uint16_t)rx[2] << 8);

    if (rx_len > BL_MAX_PAYLOAD) {
        /* Do not try to drain it: with a corrupt length there is nothing
         * trustworthy to drain. Report it and let the inter-byte timeout in the
         * next call swallow whatever is still coming. */
        return RX_BAD;
    }

    for (i = 0; i < rx_len; i++) {
        if (!bl_uart_getc(gap, &rx[3u + i])) {
            return RX_NONE;
        }
    }

    if (!bl_uart_getc(gap, &lo) || !bl_uart_getc(gap, &hi)) {
        return RX_NONE;
    }
    crc_rx = (uint16_t)lo | ((uint16_t)hi << 8);

    return (crc_rx == bl_crc16(rx, (uint16_t)(3u + rx_len))) ? RX_OK : RX_BAD;
}

/* ============================================================
 * Image validity
 * ============================================================ */

static uint32_t sig_word(uint16_t offset)
{
    return bl_nvm_read_word(BL_SIG_BASE + offset);
}

/*
 * True only if the signature row is fully written, self-consistent, and the
 * image it describes still checksums. Checked before every jump, so the
 * bootloader can never branch into blank or half-written flash.
 */
static int app_valid(void)
{
    uint32_t len, crc;

    if (sig_word(BL_SIG_OFF_MAGIC) != BL_SIG_MAGIC) {
        return 0;
    }
    if (sig_word(BL_SIG_OFF_NMAGIC) != (~BL_SIG_MAGIC & 0xFFFFFFUL)) {
        return 0;
    }
    if (sig_word(BL_SIG_OFF_VERSION) != BL_SIG_VERSION) {
        return 0;
    }

    len = sig_word(BL_SIG_OFF_LENGTH);
    if ((len == 0UL) || ((len & 1UL) != 0UL) || (len > (BL_SIG_BASE - BL_APP_BASE))) {
        return 0;
    }

    crc = sig_word(BL_SIG_OFF_CRC_LO) | (sig_word(BL_SIG_OFF_CRC_HI) << 24);

    return (bl_crc32_flash(BL_APP_BASE, len) == crc);
}

/* Stringify so the address in the goto comes from BL_APP_BASE and is not
 * written twice. The jump is inline asm and not a function-pointer call on
 * purpose: on this target a function pointer is not simply the program address,
 * and the one instruction that must be right is worth writing out. */
#define BL_STR_(x)  #x
#define BL_STR(x)   BL_STR_(x)

static void jump_to_app(void)
{
    bl_uart_deinit();               /* flushes, then hands the pins back */
    bl_time_deinit();
    sw0_release();
    led_release();

    __asm__ volatile ("goto " BL_STR(BL_APP_BASE));
}

/* ============================================================
 * Commands
 * ============================================================ */

static void cmd_sync(void)
{
    uint8_t *p = &tx[3];

    if (rx_len != 0u) {
        nak(BL_ERR_LEN);
        return;
    }

    p[0] = '3';
    p[1] = '3';
    p[2] = 'C';
    p[3] = 'K';
    p[4] = (uint8_t)BL_PROTO_VERSION;
    put32(&p[5], bl_nvm_read_word(0xFF0000UL));     /* DEVID */
    put32(&p[9], bl_nvm_read_word(0xFF0002UL));     /* DEVREV */
    put32(&p[13], BL_ERASE_STEP);
    p[17] = (uint8_t)(BL_WIRE_BLOCK_WORDS & 0xFFu);
    p[18] = (uint8_t)(BL_WIRE_BLOCK_WORDS >> 8);
    put32(&p[19], BL_APP_BASE);
    put32(&p[23], BL_SIG_BASE);
    put32(&p[27], BL_APP_END);

    respond(BL_ACK, 31u);
}

static void cmd_erase_app(void)
{
    uint32_t a;

    if (rx_len != 0u) {
        nak(BL_ERR_LEN);
        return;
    }

    /* The signature row is inside this range, so a stale signature never
     * survives an erase: from here until COMMIT the board has no valid
     * application and will not jump to one. */
    for (a = BL_APP_BASE; a < BL_APP_END; a += BL_ERASE_STEP) {
        if (!bl_nvm_erase_page(a)) {
            nak(BL_ERR_NVM);
            return;
        }
    }

    ack();
}

static void cmd_write_row(void)
{
    const uint8_t *p = RX_PAYLOAD;
    uint32_t addr, end;
    uint16_t words, i;

    if ((rx_len < 4u + 3u) || (((rx_len - 4u) % 3u) != 0u)) {
        nak(BL_ERR_LEN);
        return;
    }
    words = (uint16_t)((rx_len - 4u) / 3u);
    if (words > BL_WIRE_BLOCK_WORDS) {
        nak(BL_ERR_LEN);
        return;
    }

    addr = get32(p);
    if ((addr & 3UL) != 0UL) {
        nak(BL_ERR_ALIGN);
        return;
    }

    /* An odd word count is padded to a whole double-word with 0xFFFFFF - the
     * value erased flash already holds - so the end of an image needs no
     * special case on the host. The range check counts the pad. */
    end = addr + 2UL * (uint32_t)(words + (words & 1u));
    if ((addr < BL_APP_BASE) || (end > BL_SIG_BASE)) {
        /* BL_SIG_BASE and not BL_APP_END: the signature row belongs to COMMIT,
         * which is what makes an interrupted upload detectable. */
        nak(BL_ERR_RANGE);
        return;
    }

    for (i = 0; i < words; i += 2u) {
        const uint8_t *w0 = &p[4u + (uint16_t)(i * 3u)];
        uint32_t v0 = (uint32_t)w0[0] | ((uint32_t)w0[1] << 8) | ((uint32_t)w0[2] << 16);
        uint32_t v1 = 0xFFFFFFUL;

        if ((uint16_t)(i + 1u) < words) {
            const uint8_t *w1 = &p[4u + (uint16_t)((i + 1u) * 3u)];
            v1 = (uint32_t)w1[0] | ((uint32_t)w1[1] << 8) | ((uint32_t)w1[2] << 16);
        }

        if (!bl_nvm_write_dword(addr + 2UL * (uint32_t)i, v0, v1)) {
            nak(BL_ERR_NVM);
            return;
        }
    }

    ack();
}

static void cmd_read_crc(void)
{
    uint32_t addr, len;

    if (rx_len != 8u) {
        nak(BL_ERR_LEN);
        return;
    }

    addr = get32(RX_PAYLOAD);
    len  = get32(RX_PAYLOAD + 4);

    if (((addr | len) & 1UL) != 0UL) {
        nak(BL_ERR_ALIGN);
        return;
    }
    if ((len == 0UL) || (addr < BL_APP_BASE) || ((addr + len) > BL_APP_END)) {
        nak(BL_ERR_RANGE);
        return;
    }

    put32(&tx[3], bl_crc32_flash(addr, len));
    respond(BL_ACK, 4u);
}

static void cmd_commit(void)
{
    uint32_t len, crc;

    if (rx_len != 8u) {
        nak(BL_ERR_LEN);
        return;
    }

    len = get32(RX_PAYLOAD);
    crc = get32(RX_PAYLOAD + 4);

    if ((len == 0UL) || ((len & 1UL) != 0UL) || (len > (BL_SIG_BASE - BL_APP_BASE))) {
        nak(BL_ERR_RANGE);
        return;
    }

    /* Verify before writing, so a valid signature always means a verified
     * image. The host has usually checked the same thing with READ_CRC already;
     * doing it again here costs milliseconds and means the guarantee does not
     * depend on the host behaving. */
    if (bl_crc32_flash(BL_APP_BASE, len) != crc) {
        nak(BL_ERR_NOAPP);
        return;
    }

    /* Magic last, and as one operation with its own complement, so the row is
     * either valid or obviously not. */
    if (!bl_nvm_write_dword(BL_SIG_BASE + BL_SIG_OFF_LENGTH, len, crc & 0xFFFFFFUL)
     || !bl_nvm_write_dword(BL_SIG_BASE + BL_SIG_OFF_CRC_HI,
                            (crc >> 24) & 0xFFUL, BL_SIG_VERSION)
     || !bl_nvm_write_dword(BL_SIG_BASE + BL_SIG_OFF_MAGIC,
                            BL_SIG_MAGIC, ~BL_SIG_MAGIC & 0xFFFFFFUL)) {
        nak(BL_ERR_NVM);
        return;
    }

    ack();
}

static void cmd_jump(void)
{
    if (rx_len != 0u) {
        nak(BL_ERR_LEN);
        return;
    }
    if (!app_valid()) {
        nak(BL_ERR_NOAPP);
        return;
    }

    ack();                          /* answered before the jump; deinit flushes it */
    jump_to_app();
}

static void dispatch(void)
{
    switch (RX_CMD) {
    case BL_CMD_SYNC:       cmd_sync();       break;
    case BL_CMD_ERASE_APP:  cmd_erase_app();  break;
    case BL_CMD_WRITE_ROW:  cmd_write_row();  break;
    case BL_CMD_READ_CRC:   cmd_read_crc();   break;
    case BL_CMD_COMMIT:     cmd_commit();     break;
    case BL_CMD_JUMP:       cmd_jump();       break;
    default:                nak(BL_ERR_CMD);  break;
    }
}

/* ============================================================
 * Entry
 * ============================================================ */

int main(void)
{
    uint16_t window = BL_MS_TO_TICKS(BL_WINDOW_MS);
    uint16_t t0;
    rx_result_t r = RX_NONE;
    int stay;

    /* No PLL, no clock switch: FNOSC = FRC and FRCDIV defaults to /1, so
     * FCY = 4 MHz. Set FRCDIV anyway rather than trust a reset default that a
     * future silicon revision could change. */
    CLKDIVbits.FRCDIV = 0b000;

    led_init();
    led(1);                         /* on for as long as the bootloader is in charge */
    bl_time_init();
    sw0_init();
    bl_uart_init();

    /* SW0 held at power-up means "stay in the bootloader whatever happens" -
     * the only way in when the running sketch never reads its serial port. On
     * this board there is no reset button, so a power cycle means unplugging
     * USB; that is documented, not hidden. */
    stay = sw0_pressed();

    /* Otherwise: a short window at every reset. The host does not depend on a
     * human hitting it - it asks the running sketch to reset itself and then
     * floods SYNC frames, so the window only has to outlast one frame. */
    t0 = bl_ticks();
    while (!stay && (bl_ticks_since(t0) < window)) {
        r = rx_frame((uint16_t)(window - bl_ticks_since(t0)));
        if (r != RX_NONE) {
            stay = 1;               /* a host is talking, good frame or not */
        }
    }

    if (!stay && app_valid()) {
        jump_to_app();
    }

    /* Either a host is here, or there is nothing valid to run. Both mean: wait
     * for commands, indefinitely, blinking so the board says which state it is
     * in. Every wait inside the loop is bounded, so the blink keeps going. */
    for (;;) {
        if (r == RX_OK) {
            led(1);
            dispatch();
        } else if (r == RX_BAD) {
            led(1);
            nak(BL_ERR_CRC);
        }

        r = rx_frame(BL_MS_TO_TICKS(100));
        if (r == RX_NONE) {
            led((bl_ticks() & 0x1000u) ? 1 : 0);    /* ~2 Hz */
        }
    }
}
