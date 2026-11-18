/*
 * Lab 32 - PPS: Peripheral Pin Select
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  On most small chips each peripheral sits on a fixed pin. On this one
 *        almost every digital peripheral can be connected to almost any pin,
 *        and changed while the program runs. In this lab:
 *        1. Move a PWM signal from pin to pin (and onto two pins at once).
 *        2. Move an INPUT: the same interrupt routine, fed from another pin.
 *        3. Wire peripherals together INSIDE the chip with virtual pins:
 *           measure a PWM frequency and loop a UART back with no wire at all.
 *        4. Lock the mapping and see that writes are ignored.
 *        5. Print the chip's whole pin map, including what the core set up.
 *
 * Hardware: nothing needed for 1, 3, 4, 5 (uses the USER LED and button).
 *   For 2b: a jumper wire on D8 = RB3 (J3-16) that you touch to GND (J3-2).
 *   Optional: LED + 1k from D9 = RB4 (J3-15) to GND, to see the PWM move.
 *
 * Serial commands (Serial Monitor on J5, 115200):
 *   m   print the pin map (all PPS outputs and inputs in use)
 *   l   PWM (SCCP1) on the USER LED  on / off      -> the LED "breathes"
 *   9   PWM on D9 = RB4 (J3-15)      on / off      -> both at once is fine
 *   i   INT1 input: USER button (RC12) <-> D8 = RB3 (J3-16)
 *   f   frequency of the PWM, measured through virtual pin RPV0 (no wire)
 *   u   UART2 loop-back through virtual pin RPV1 (no wire)
 *   k   lock test: IOLOCK = 1, try to move the PWM, read it back
 *
 * ----------------------------------------------------------------------------
 * WHAT IS CONNECTED IN THIS SKETCH
 *   SCCP1 PWM 1 kHz (OCM1, code 15) -> RC13 USER LED (RP61) and/or RB4 D9 (RP36)
 *                                   -> RPV0 (RP176) -> INT2 input: counts edges
 *   INT1 input <- RC12 USER button (RP60), or RB3 D8 (RP35)
 *   UART2 TX (code 3) -> RPV1 (RP177) -> UART2 RX input
 *   The core already connected UART1 for Serial: U1TX -> RC10, U1RX <- RC11.
 *   ('m' shows them too.)
 *   The PPS.h tab explains the registers.
 * NOT VERIFIED on hardware. The MPLAB simulator stores every PPS register
 * correctly ('m' is right) but does not pass signals through PPS, so 'f' reads
 * 0 Hz, 'u' fails and 'k' looks unlocked there. On a real chip 'f' should say
 * 1000 Hz and 'u' should echo the text.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "PPS.h"

const unsigned long BAUD = 115200;
const uint8_t RP_LED = 61, RP_D9 = 36, RP_BUTTON = 60, RP_D8 = 35;
const uint8_t RPV0 = 176, RPV1 = 177;

/* ---------------- SCCP1: a 1 kHz PWM to move around ---------------- */
#if FCY > 20000000UL
const uint8_t PWM_PS = 0b01; const unsigned long PWM_DIV = 4;   /* 1:4 */
#else
const uint8_t PWM_PS = 0b00; const unsigned long PWM_DIV = 1;
#endif
const uint16_t PWM_PERIOD = (uint16_t)(FCY / PWM_DIV / 1000UL - 1);    /* 1 kHz */

void pwmBegin()
{
    _CCP1MD = 0;
    CCP1CON1L = 0; CCP1CON1H = 0; CCP1CON2L = 0; CCP1CON2H = 0;
    CCP1CON1Lbits.MOD = 0b0101;             /* dual edge compare, buffered = PWM */
    CCP1CON1Lbits.TMRPS = PWM_PS;
    CCP1CON1Lbits.CLKSEL = 0b000;           /* FCY */
    CCP1CON2Hbits.OCAEN = 1;                /* output A -> the "OCM1" PPS signal */
    CCP1PRL = PWM_PERIOD;
    CCP1PRH = 0xFFFF;
    CCP1RA = 0;
    CCP1RB = PWM_PERIOD / 2;
    CCP1CON1Lbits.CCPON = 1;
}
inline void pwmDuty(uint16_t d) { CCP1RB = d; }

/* ---------------- interrupts ---------------- */
volatile unsigned long int1Count = 0, int2Count = 0;
extern "C" void __attribute__((interrupt, no_auto_psv)) _INT1Interrupt(void) { int1Count++; _INT1IF = 0; }
extern "C" void __attribute__((interrupt, no_auto_psv)) _INT2Interrupt(void) { int2Count++; _INT2IF = 0; }

bool int1OnButton = true;

void int1Select(bool button)
{
    int1OnButton = button;
    if (button) ppsInput(_INT1R, RP_BUTTON);
    else { pinMode(8, INPUT_PULLUP); ppsInput(_INT1R, RP_D8); }
    _INT1IF = 0;
    Serial.print("# INT1 now listens to ");
    Serial.println(button ? "RC12 = USER button" : "RB3 = D8 (J3-16): touch the wire to GND");
}

/* ---------------- UART2, polled, for the loop-back ---------------- */
void uart2Begin(unsigned long baud)
{
    _U2MD = 0;
    U2MODE = 0; U2MODEH = 0; U2STA = 0; U2STAH = 0;
    U2MODEHbits.BCLKMOD = 1;                /* fractional: baud = FCY / BRG */
    unsigned long brg = FCY / baud;
    U2BRG = (uint16_t)brg; U2BRGH = (uint16_t)(brg >> 16);
    U2MODEbits.UARTEN = 1; U2MODEbits.UTXEN = 1; U2MODEbits.URXEN = 1;
}

void uartLoopback()
{
    ppsOutput(RPV1, PPS_U2TX);              /* U2TX -> virtual pin RPV1 */
    ppsInput(_U2RXR, RPV1);                 /* U2RX <- virtual pin RPV1 */
    uart2Begin(115200);
    while (!U2STAHbits.URXBE) (void)U2RXREG;    /* empty the receiver */

    const char *msg = "Hello through RPV1!";
    char got[32]; uint8_t n = 0;
    for (const char *p = msg; *p; p++) {
        while (U2STAHbits.UTXBF) { }
        U2TXREG = *p;
        unsigned long t0 = micros();        /* read each byte back as it arrives */
        while (U2STAHbits.URXBE && micros() - t0 < 2000) { }
        if (!U2STAHbits.URXBE && n < sizeof(got) - 1) got[n++] = (char)U2RXREG;
    }
    got[n] = 0;
    Serial.print("# UART2 sent \""); Serial.print(msg); Serial.print("\", received \"");
    Serial.print(got); Serial.println("\"");
    Serial.println(strcmp(msg, got) == 0 ? "  -> loop-back OK, with no wire on the board" : "  -> loop-back FAILED");
}

/* ---------------- frequency through a virtual pin ---------------- */
void measureFrequency()
{
    ppsOutput(RPV0, PPS_OCM1);              /* the PWM also goes to RPV0 ... */
    ppsInput(_INT2R, RPV0);                 /* ... and INT2 listens to RPV0 */
    _INT2EP = 0;                            /* rising edge */
    _INT2IF = 0; _INT2IE = 1;
    noInterrupts(); int2Count = 0; interrupts();
    delay(1000);
    unsigned long n = int2Count;
    Serial.print("# PWM measured inside the chip: "); Serial.print(n);
    Serial.print(" rising edges in 1 s = "); Serial.print(n); Serial.println(" Hz (set: 1000 Hz)");
}

/* ---------------- lock test ---------------- */
void lockTest()
{
    uint8_t before = ppsOutputOf(RP_D9);
    ppsLock();
    *ppsOutReg(RP_D9) = (before == PPS_OCM1) ? PPS_NONE : PPS_OCM1;   /* try to change it */
    uint8_t locked = ppsOutputOf(RP_D9);
    ppsUnlock();
    *ppsOutReg(RP_D9) = (before == PPS_OCM1) ? PPS_NONE : PPS_OCM1;
    uint8_t open = ppsOutputOf(RP_D9);
    *ppsOutReg(RP_D9) = before;             /* put it back */
    ppsLock();
    Serial.print("# RP36R was "); Serial.print(before);
    Serial.print(". Write while LOCKED -> reads "); Serial.print(locked);
    Serial.print(locked == before ? " (ignored)" : " (CHANGED?!)");
    Serial.print(". Write while UNLOCKED -> reads "); Serial.print(open);
    Serial.println(open != before ? " (changed)" : " (not changed?!)");
}

void help()
{
    Serial.println();
    Serial.println("Lab 32 - Peripheral Pin Select");
    Serial.println("  m map  l PWM on LED  9 PWM on D9  i move INT1  f measure via RPV0  u UART via RPV1  k lock test");
}

bool pwmOnLed = true, pwmOnD9 = false;

void setup()
{
    Serial.begin(BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(9, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    pwmBegin();
    ppsOutput(RP_LED, PPS_OCM1);            /* start with the PWM on the LED */

    _INT1EP = 1;                            /* falling edge = button pressed */
    int1Select(true);
    _INT1IF = 0; _INT1IP = 2; _INT1IE = 1;

    help();
    ppsDump();
}

unsigned long lastInt1 = 0;

void loop()
{
    /* breathe: the duty cycle goes up and down once every 2 s */
    unsigned int phase = (millis() / 4) % 500;                  /* 0..499 */
    unsigned int level = phase < 250 ? phase : 499 - phase;     /* 0..249..0 */
    pwmDuty((uint16_t)((unsigned long)level * level * PWM_PERIOD / (249UL * 249UL)));

    if (int1Count != lastInt1) {
        lastInt1 = int1Count;
        Serial.print("# INT1 interrupt "); Serial.print(int1Count);
        Serial.println(int1OnButton ? " (from the USER button)" : " (from D8)");
    }

    if (!Serial.available()) return;
    char c = Serial.read();
    switch (c) {
    case 'm': ppsDump(); break;
    case 'l':
        pwmOnLed = !pwmOnLed;
        ppsOutput(RP_LED, pwmOnLed ? PPS_OCM1 : PPS_NONE);
        if (!pwmOnLed) digitalWrite(LED_BUILTIN, LED_OFF);     /* pin back to LATC13 */
        Serial.println(pwmOnLed ? "# PWM -> USER LED" : "# USER LED is a normal port pin again (off)");
        break;
    case '9':
        pwmOnD9 = !pwmOnD9;
        ppsOutput(RP_D9, pwmOnD9 ? PPS_OCM1 : PPS_NONE);
        if (!pwmOnD9) digitalWrite(9, LOW);
        Serial.println(pwmOnD9 ? "# PWM -> D9 (J3-15) as well" : "# D9 is a normal port pin again (low)");
        break;
    case 'i': int1Select(!int1OnButton); break;
    case 'f': measureFrequency(); break;
    case 'u': uartLoopback(); break;
    case 'k': lockTest(); break;
    default: break;
    }
}
