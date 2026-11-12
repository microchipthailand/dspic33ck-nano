/*
 * wiring_digital.c - Digital I/O for dsPIC33CK Arduino core
 *
 * Implements: pinMode, digitalWrite, digitalRead
 * Uses the pin_map_t table from variant.c for port/bit lookup.
 */

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pull-up enable register for a port, or NULL if the port has none.
 *
 * The CNPUx registers are per-port and not contiguous, so they cannot be
 * indexed off the PORT pointer -- they have to be selected by port. Which ones
 * exist varies across the family: the 28-pin parts (MC002, MP102) have only A
 * and B, MC005 adds C and D, and MP508 adds E. The device header defines a
 * self-named macro for every register the part actually has ("#define CNPUD
 * CNPUD"), so guarding on that keeps all four variants compiling.
 *
 * The previous code tested only for PORTB and fell back to CNPUA for every
 * other port, which meant INPUT_PULLUP on, say, RD13 set the pull-up on RA13
 * and left RD13 floating -- a floating input reads LOW, so a button on any
 * port above B looked permanently pressed. */
static volatile uint16_t *cnpu_for_port(const volatile uint16_t *port_reg)
{
    if (port_reg == &PORTA) return &CNPUA;
    if (port_reg == &PORTB) return &CNPUB;
#if defined(CNPUC)
    if (port_reg == &PORTC) return &CNPUC;
#endif
#if defined(CNPUD)
    if (port_reg == &PORTD) return &CNPUD;
#endif
#if defined(CNPUE)
    if (port_reg == &PORTE) return &CNPUE;
#endif
    return NULL;
}

void pinMode(uint8_t pin, uint8_t mode)
{
    if (pin >= NUM_DIGITAL_PINS) return;

    const pin_map_t *p = &g_pin_map[pin];
    volatile uint16_t *cnpu;

    /* If pin has analog capability, set it to digital mode */
    if (p->ansel_reg != NULL) {
        *(p->ansel_reg) &= ~(1U << p->bit);
    }

    switch (mode) {
        case INPUT:
            *(p->tris_reg) |= (1U << p->bit);   /* TRIS=1 -> input */
            /* Disable internal pull-up (CNPU register) */
            cnpu = cnpu_for_port(p->port_reg);
            if (cnpu != NULL) {
                *cnpu &= ~(1U << p->bit);
            }
            break;

        case INPUT_PULLUP:
            *(p->tris_reg) |= (1U << p->bit);   /* TRIS=1 -> input */
            /* Enable internal pull-up */
            cnpu = cnpu_for_port(p->port_reg);
            if (cnpu != NULL) {
                *cnpu |= (1U << p->bit);
            }
            break;

        case OUTPUT:
        default:
            *(p->tris_reg) &= ~(1U << p->bit);  /* TRIS=0 -> output */
            break;
    }
}

void digitalWrite(uint8_t pin, uint8_t val)
{
    if (pin >= NUM_DIGITAL_PINS) return;

    const pin_map_t *p = &g_pin_map[pin];

    if (val == HIGH) {
        *(p->lat_reg) |= (1U << p->bit);
    } else {
        *(p->lat_reg) &= ~(1U << p->bit);
    }
}

int digitalRead(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) return LOW;

    const pin_map_t *p = &g_pin_map[pin];

    return (*(p->port_reg) & (1U << p->bit)) ? HIGH : LOW;
}

/*
 * Peripheral Pin Select number for an Arduino pin, or -1 if it has none.
 *
 * The rule is positional and per-port: RBn is RP(32+n), RCn is RP(48+n), RDn is
 * RP(64+n). PORTA carries no RPn at all on any device in this family, and neither
 * does PORTE on the MP508 -- the RP176-RP181 numbers that look like they should
 * be PORTE are the virtual pins RPV0-RPV5, internal nodes with no bond wire, so
 * PPS can route a peripheral to them but nothing outside the die will ever see
 * it. Those return -1 here, which is the honest answer.
 *
 * Validity is decided by which PORTx registers the device has, NOT by whether the
 * _RPnnR macros exist: p33CK32MP102.h defines _RP48R through _RP77R even though
 * that part has only PORTA and PORTB, so the macros prove nothing about what is
 * bonded out.
 *
 * Nothing in the core needs this yet -- Change Notification does not go through
 * PPS. It is here because the RP rule was previously encoded implicitly in the
 * PWMn_RP constants in each variant header and nowhere else, and because the
 * hardware-PWM tone path (see wiring_tone.c) needs exactly this lookup.
 */
int16_t pinToRP(uint8_t pin)
{
    const pin_map_t *p;

    if (pin >= NUM_DIGITAL_PINS) return -1;

    p = &g_pin_map[pin];

    if (p->port_reg == &PORTB) return (int16_t)(32 + p->bit);
#if defined(PORTC)
    if (p->port_reg == &PORTC) return (int16_t)(48 + p->bit);
#endif
#if defined(PORTD)
    if (p->port_reg == &PORTD) return (int16_t)(64 + p->bit);
#endif
    return -1;
}

#ifdef __cplusplus
}
#endif
