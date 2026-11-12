/*
 * wiring_interrupts.c - attachInterrupt/detachInterrupt and the global interrupt
 *                       enable, for the dsPIC33CK Arduino core
 *
 * The counterpart of upstream's WInterrupts.c. interrupts()/noInterrupts() live
 * here too, so that wiring.c -- which owns Timer1 and a pair of DISI critical
 * sections that are easy to break -- needs no edits at all for this.
 */

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================== */
/* Global interrupt enable                                                */
/* ====================================================================== */

/*
 * GIE is INTCON2[15], R/W, POR = 1. A single bit in a bit-addressable SFR, so
 * each of these compiles to one BSET/BCLR and is inherently atomic.
 *
 * DELIBERATELY NOT __builtin_disi(). DISI has three properties that make it the
 * wrong primitive for a user-facing critical section:
 *
 *   - It expires. "DISI #n" re-enables after n cycles, so a section that runs
 *     longer than the count silently loses its protection partway through.
 *   - It masks only priority levels 1-6, so it cannot hold off everything.
 *   - It is a single global with no nesting. millis() uses a DISI pair of its own
 *     (wiring.c) to read its 32-bit counter atomically, and the trailing "disi #0"
 *     there would RE-ENABLE interrupts in the middle of a user's noInterrupts()
 *     section.
 *
 * GIE and DISI are independent -- INTCON2's DISI bit is a separate read-only
 * status bit -- so the two compose: millis() still reads atomically inside a
 * noInterrupts()/interrupts() pair, and wiring.c must be left exactly as it is.
 *
 * Caveats, all of them shared with stock Arduino:
 *   - Pending IFx flags latch while GIE is 0, so no event is lost.
 *   - But Timer1 only ever adds one to the millis() counter per interrupt, and
 *     it rolls over every ~1 ms. Keep interrupts off longer than that and
 *     millis() loses time; delay(), which spins on millis(), never returns.
 *   - No nesting counter, matching upstream's unconditional sei/cli. Nested
 *     noInterrupts() sections do not restore, they just enable.
 */
void interrupts(void)   { INTCON2bits.GIE = 1; }
void noInterrupts(void) { INTCON2bits.GIE = 0; }

/* ====================================================================== */
/* attachInterrupt / detachInterrupt, over Change Notification            */
/* ====================================================================== */

/*
 * Change Notification rather than the remappable INT1/INT2/INT3, because CN is a
 * strict superset of what INTx could offer here:
 *
 *   - Reach. INTx has to arrive through PPS, and PORTA has no RPn at all on any
 *     device in this family (nor does PORTE on the MP508). CN covers every pin
 *     on every port.
 *   - Count. INTx gives three handlers -- INT0 is welded to a pin that is also
 *     PWM3 -- against which CN offers every pin at once and needs no allocator,
 *     so attachInterrupt() cannot fail for lack of hardware.
 *   - CHANGE. This is the decisive one. INTxEP is a single polarity bit, so
 *     CHANGE would have to be emulated by flipping it inside the handler, which
 *     is not merely inelegant but wrong: if the line returns to its original
 *     level before the flip lands, the polarity is left inverted permanently with
 *     nothing to self-correct it. That is the classic rotary-encoder and
 *     IR-receiver failure. CN does both edges natively.
 *
 * Priority IPL2, below millis() (IPL4) and Serial RX (IPL3). That choice is what
 * makes handlers actually usable: millis() keeps returning a live value,
 * delay() works instead of deadlocking (it spins on millis(), which Timer1 still
 * advances), and Serial.print() works because _serial_write polls U1STAHbits.UTXBF.
 *
 * Serial.print() from a handler is NOT reentrant, though: preempting a mainline
 * print interleaves the two outputs mid-string. It will not hang or corrupt
 * anything, but it will look like garbage.
 */

#define CN_IPL      2

typedef void (*_isr_fn)(void);

/*
 * Which CN ports this device actually has.
 *
 * Guarded on CNCONx, and NEVER on the interrupt-bit macros. p33CK32MP102.h
 * defines _CNCIF/_CNDIF/_CNCIE/_CNDIE and its linker script even carries
 * __CNCInterrupt and __CNDInterrupt vector slots, although that part has no
 * PORTC or PORTD at all. CNCONx is the macro that tracks the ports that exist.
 * Same idiom as cnpu_for_port() in wiring_digital.c.
 *
 * Ports by device: A,B on MP102 and MC002; A,B,C,D on MC005; A,B,C,D,E on MP508.
 */
enum {
    _CN_A = 0,
    _CN_B,
#if defined(CNCONC)
    _CN_C,
#endif
#if defined(CNCOND)
    _CN_D,
#endif
#if defined(CNCONE)
    _CN_E,
#endif
    _CN_NPORTS
};

typedef struct {
    volatile uint16_t *port_reg;    /* identity key against g_pin_map[].port_reg */
    volatile uint16_t *cncon;
    volatile uint16_t *cnen0;
    volatile uint16_t *cnen1;
    volatile uint16_t *cnf;
    _isr_fn           *tbl;         /* 16 slots, indexed by bit position */
} cn_port_t;

/*
 * Flat tables rather than a compacted list, because the ISR indexes them
 * directly by bit position and must not go searching.
 *
 * 64 bytes of RAM per port -- a function pointer is 4 bytes here, not 2, since a
 * program address on this architecture needs 24 bits. So 128 B on MP102 and
 * MC002, 256 B on MC005, 320 B on MP508 (measured, not estimated).
 *
 * Every sketch pays that whether or not it calls attachInterrupt(): core.a is
 * linked with --whole-archive, the CN vectors below are reachable from the
 * interrupt vector table, and each one references its own table, so --gc-sections
 * cannot drop them. 128 B out of the MP102's 4 KB is the worst case. If that ever
 * needs to go, the fix is at the link level -- drop --whole-archive and pull the
 * ISR objects in with -u -- not here.
 */
static _isr_fn _cn_tbl_a[16];
static _isr_fn _cn_tbl_b[16];
#if defined(CNCONC)
static _isr_fn _cn_tbl_c[16];
#endif
#if defined(CNCOND)
static _isr_fn _cn_tbl_d[16];
#endif
#if defined(CNCONE)
static _isr_fn _cn_tbl_e[16];
#endif

static const cn_port_t _cn_ports[] = {
    { &PORTA, &CNCONA, &CNEN0A, &CNEN1A, &CNFA, _cn_tbl_a },
    { &PORTB, &CNCONB, &CNEN0B, &CNEN1B, &CNFB, _cn_tbl_b },
#if defined(CNCONC)
    { &PORTC, &CNCONC, &CNEN0C, &CNEN1C, &CNFC, _cn_tbl_c },
#endif
#if defined(CNCOND)
    { &PORTD, &CNCOND, &CNEN0D, &CNEN1D, &CNFD, _cn_tbl_d },
#endif
#if defined(CNCONE)
    { &PORTE, &CNCONE, &CNEN0E, &CNEN1E, &CNFE, _cn_tbl_e },
#endif
};

/* CNCONx bits. Everything below bit 11 is unimplemented and reads as 0. */
#define CNCON_ON        0x8000U     /* bit 15: CN module enabled                */
#define CNCON_STYLE     0x0800U     /* bit 11: 1 = edge style (uses CNFx)       */

/*
 * Priority and enable for one port's vector.
 *
 * IE, IF and IP are single bits scattered across six different registers at
 * offsets that differ per port -- CNAIE/CNBIE in IEC0, CNCIE in IEC1,
 * CNDIE/CNEIE in IEC4, and so on -- so they cannot be reached through the
 * pointer table above. Switching on the port index keeps the device header's own
 * bitfield names as the single source of truth for where each bit actually lives,
 * which matters because those indices are not guessable and not uniform.
 *
 * Setting the priority on every call is redundant but idempotent, and cheaper
 * than a second switch.
 */
static void _cn_ie(uint8_t idx, uint8_t on)
{
    switch (idx) {
        case _CN_A:
            IPC0bits.CNAIP = CN_IPL;
            IEC0bits.CNAIE = on;
            break;
        case _CN_B:
            IPC0bits.CNBIP = CN_IPL;
            IEC0bits.CNBIE = on;
            break;
#if defined(CNCONC)
        case _CN_C:
            IPC4bits.CNCIP = CN_IPL;
            IEC1bits.CNCIE = on;
            break;
#endif
#if defined(CNCOND)
        case _CN_D:
            IPC18bits.CNDIP = CN_IPL;
            IEC4bits.CNDIE = on;
            break;
#endif
#if defined(CNCONE)
        case _CN_E:
            IPC19bits.CNEIP = CN_IPL;
            IEC4bits.CNEIE = on;
            break;
#endif
        default:
            break;
    }
}

/*
 * Clear one port's summary flag.
 *
 * Called ONLY from the path where a port's last handler has just been detached.
 * It is not safe anywhere else, and in particular not on the way in to
 * attachInterrupt(): CNxIF summarises the per-pin CNFx latches, so clearing it
 * while another pin on the same port still has a handler attached would discard
 * the interrupt request for an edge whose CNFx bit is already set -- and nothing
 * re-asserts it. That edge would be lost. On the detach-empty path every CNEN bit
 * is clear and every CNFx bit has been cleared as its pin was detached, so there
 * is provably nothing pending.
 */
static void _cn_if_clear(uint8_t idx)
{
    switch (idx) {
        case _CN_A: IFS0bits.CNAIF = 0; break;
        case _CN_B: IFS0bits.CNBIF = 0; break;
#if defined(CNCONC)
        case _CN_C: IFS1bits.CNCIF = 0; break;
#endif
#if defined(CNCOND)
        case _CN_D: IFS4bits.CNDIF = 0; break;
#endif
#if defined(CNCONE)
        case _CN_E: IFS4bits.CNEIF = 0; break;
#endif
        default: break;
    }
}

/*
 * Resolve an Arduino pin to its CN port descriptor, by comparing the PORTx
 * register address the pin map already carries.
 *
 * Pointer identity rather than pin-number arithmetic, because there is no
 * arithmetic relationship to exploit: mc005's PORTD entries are D35=RD1,
 * D36=RD8, D37=RD10, D38=RD13. Same reason cnpu_for_port() in wiring_digital.c
 * works this way.
 *
 * Returns NULL for a pin on a port with no CN hardware, which is why callers can
 * treat "no descriptor" as "silently do nothing".
 */
static const cn_port_t *_cn_find(uint8_t pin, uint8_t *idx_out)
{
    volatile uint16_t *pr = g_pin_map[pin].port_reg;
    uint8_t i;

    for (i = 0U; i < (uint8_t)_CN_NPORTS; i++) {
        if (_cn_ports[i].port_reg == pr) {
            *idx_out = i;
            return &_cn_ports[i];
        }
    }
    return NULL;
}

void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode)
{
    const cn_port_t *cp;
    uint8_t idx = 0U;
    uint8_t bit;
    uint16_t mask;

    /* The signature returns void, so every rejection here is a silent no-op --
     * the same contract as upstream. */
    if (pin >= NUM_DIGITAL_PINS) return;
    if (userFunc == NULL) return;
    if (mode != CHANGE && mode != FALLING && mode != RISING) return;

    cp = _cn_find(pin, &idx);
    if (cp == NULL) return;

    bit  = g_pin_map[pin].bit;
    mask = (uint16_t)(1U << bit);

    /*
     * Making the pin an input is part of arming the interrupt, not a
     * convenience: "The CN interrupt is generated only for the I/Os configured
     * as inputs (corresponding TRISx bits must be set)".
     *
     * Done inline rather than via pinMode(pin, INPUT), because that clears CNPUx
     * and would silently break the single most common idiom there is:
     *
     *     pinMode(p, INPUT_PULLUP);
     *     attachInterrupt(digitalPinToInterrupt(p), fn, FALLING);
     *
     * CNPUx is left exactly as the sketch set it. (A cleared pull-up on a button
     * input reads as permanently pressed -- that exact bug cost a bench session
     * in Phase 13.)
     */
    if (g_pin_map[pin].ansel_reg != NULL) {
        *(g_pin_map[pin].ansel_reg) &= (uint16_t)~mask;
    }
    *(g_pin_map[pin].tris_reg) |= mask;

    /* Mask this port's vector for the reconfiguration. Re-arming a pin that is
     * already attached passes through a transient CNEN state, and an edge caught
     * there would dispatch with the old mode. Other ports keep running. */
    _cn_ie(idx, 0U);

    /* Handler in place before the pin can possibly fire. */
    cp->tbl[bit] = userFunc;

    /* Edge style, so the CNFx per-pin latches are live and the CNEN1:CNEN0 pair
     * selects which edges count (Table 8-3):
     *     0:0 disabled   0:1 positive only   1:0 negative only   1:1 both
     * Arduino.h happens to number RISING 3, FALLING 2, CHANGE 1, so those two
     * bits could be sliced straight out of the mode value -- spelled out instead,
     * because that is a coincidence and not a contract. */
    *(cp->cncon) |= CNCON_STYLE;

    if (mode == RISING) {
        *(cp->cnen1) &= (uint16_t)~mask;
        *(cp->cnen0) |= mask;
    } else if (mode == FALLING) {
        *(cp->cnen1) |= mask;
        *(cp->cnen0) &= (uint16_t)~mask;
    } else {                                /* CHANGE */
        *(cp->cnen1) |= mask;
        *(cp->cnen0) |= mask;
    }

    /* Discard an edge manufactured by the setup itself -- this pin's latch only,
     * never the whole register. */
    *(cp->cnf) &= (uint16_t)~mask;

    *(cp->cncon) |= CNCON_ON;

    /* Note what is deliberately NOT here: a CNxIF clear. If a sibling pin's edge
     * arrived while the vector was masked, its CNFx bit is set and CNxIF is
     * pending; enabling IE with the flag still set services it immediately.
     * Clearing it would throw that edge away. A spurious entry that finds nothing
     * pending is harmless -- _cn_service() handles it. */
    _cn_ie(idx, 1U);
}

void detachInterrupt(uint8_t pin)
{
    const cn_port_t *cp;
    uint8_t idx = 0U;
    uint8_t bit, i, any;
    uint16_t mask;

    if (pin >= NUM_DIGITAL_PINS) return;

    cp = _cn_find(pin, &idx);
    if (cp == NULL) return;

    bit  = g_pin_map[pin].bit;
    mask = (uint16_t)(1U << bit);

    _cn_ie(idx, 0U);

    *(cp->cnen0) &= (uint16_t)~mask;
    *(cp->cnen1) &= (uint16_t)~mask;
    *(cp->cnf)   &= (uint16_t)~mask;
    cp->tbl[bit]  = NULL;

    /* TRISx and CNPUx are left as the sketch set them. detachInterrupt() is not
     * pinMode(): a sketch that goes on polling digitalRead() on this pin should
     * still find it an input with its pull-up intact. */

    any = 0U;
    for (i = 0U; i < 16U; i++) {
        if (cp->tbl[i] != NULL) { any = 1U; break; }
    }

    if (any) {
        _cn_ie(idx, 1U);
    } else {
        /* Last handler on this port: shut the module down and, only now that
         * nothing can be pending, clear the summary flag. IE stays off. */
        *(cp->cncon) &= (uint16_t)~CNCON_ON;
        _cn_if_clear(idx);
    }
}

/*
 * Dispatch one port's pending edges.
 *
 * A single pass, on purpose. A "while (*cnf)" loop would let a fast signal -- a
 * bouncing switch, an encoder at speed -- hold this ISR forever and starve
 * everything below IPL2, including millis(). Anything that arrives after the
 * snapshot has its own CNFx bit set and raises the vector again, so nothing is
 * lost by returning.
 *
 * Only the bits in the snapshot are cleared, never the whole register: an edge
 * landing between the read and the write keeps its latch.
 */
static void _cn_service(volatile uint16_t *cnf, _isr_fn *tbl)
{
    uint16_t pending = *cnf;
    uint8_t bit;

    if (pending == 0U) return;

    *cnf &= (uint16_t)~pending;

    for (bit = 0U; bit < 16U; bit++) {
        if ((pending & (uint16_t)(1U << bit)) != 0U && tbl[bit] != NULL) {
            tbl[bit]();
        }
    }
}

/*
 * THE SUMMARY FLAG IS CLEARED FIRST HERE. That inverts the idiom used everywhere
 * else in this core -- wiring.c's _T1Interrupt clears T1IF last, which is the
 * conventional and correct order for an ordinary peripheral.
 *
 * It is deliberate, and please do not "fix" it back for consistency.
 *
 * CNxIF is not an event flag, it is a SUMMARY of the per-pin CNFx latches. With
 * flag-last ordering, an edge arriving after _cn_service() has snapshotted CNFx
 * but before CNxIF is cleared ends up latched in CNFx with its interrupt request
 * thrown away -- and nothing re-asserts CNxIF, so that edge is lost for good.
 * Clearing the summary first means the worst case is one spurious re-entry that
 * finds CNFx empty, which _cn_service() returns from immediately.
 *
 * auto_psv, unlike wiring_tone.c's ISR: these dispatch into arbitrary user code
 * that may well read const data out of program space, so PSVPAG has to be valid.
 */
void __attribute__((interrupt, auto_psv)) _CNAInterrupt(void)
{
    IFS0bits.CNAIF = 0;
    _cn_service(&CNFA, _cn_tbl_a);
}

void __attribute__((interrupt, auto_psv)) _CNBInterrupt(void)
{
    IFS0bits.CNBIF = 0;
    _cn_service(&CNFB, _cn_tbl_b);
}

#if defined(CNCONC)
void __attribute__((interrupt, auto_psv)) _CNCInterrupt(void)
{
    IFS1bits.CNCIF = 0;
    _cn_service(&CNFC, _cn_tbl_c);
}
#endif

#if defined(CNCOND)
void __attribute__((interrupt, auto_psv)) _CNDInterrupt(void)
{
    IFS4bits.CNDIF = 0;
    _cn_service(&CNFD, _cn_tbl_d);
}
#endif

#if defined(CNCONE)
void __attribute__((interrupt, auto_psv)) _CNEInterrupt(void)
{
    IFS4bits.CNEIF = 0;
    _cn_service(&CNFE, _cn_tbl_e);
}
#endif

#ifdef __cplusplus
}
#endif
