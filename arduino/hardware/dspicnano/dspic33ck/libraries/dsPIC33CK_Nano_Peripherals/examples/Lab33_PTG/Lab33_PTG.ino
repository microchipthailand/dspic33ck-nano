/*
 * Lab 33 - PTG: the Peripheral Trigger Generator
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  1. Write a program for the PTG - a small sequencer inside the chip -
 *           and let it blink the LED with NO CPU time at all.
 *        2. Make it react to a pin: the USER button starts a burst of flashes.
 *        3. Step it from the CPU with a software trigger.
 *        4. Prove it is independent: switch interrupts off and keep the CPU
 *           busy for 3 s - the PTG timing does not even notice.
 *
 * Hardware: nothing to wire for programs 1-3 (USER LED, USER button).
 *   Optional: scope / logic analyser / LED + 1k on D9 = RB4 (J3-15) for the
 *   second PTG output (program 4: a 1 kHz square wave).
 *
 * Serial commands (Serial Monitor on J5, 115200):
 *   1   program 1: blink the LED at 2 Hz (Timer0 + counter 0 loop)
 *   2   program 2: press USER -> 5 flashes (waits for the INT2 pin)
 *   3   program 3: each 'g' moves the PTG one round (software trigger)
 *   4   program 4: 1 kHz square wave on D9 + LED blink, both from the PTG
 *   g   software trigger (for program 3)
 *   b   busy test: interrupts OFF, CPU stuck for 3 s - watch the LED
 *   l   list the PTG program that is loaded
 *   ?   status
 *
 * ----------------------------------------------------------------------------
 * HOW IT IS WIRED INSIDE (no wires on the board)
 *   PTG output 24  --PPS code 46-->  RC13 = USER LED     (RPOR register of RP61)
 *   PTG output 25  --PPS code 47-->  RB4  = D9 (J3-15)   (RP36)
 *   USER button RC12 (RP60) --PPS--> INT2 input  --> PTG input 15
 *   PTG interrupts 0 and 1 --> the CPU, only to count events for the print-out
 *   PTGTOGL = 1: every "trigger output" step toggles that output.
 *
 * PROGRAM 1 (blink)                   PTG clock = FCY / 32 (3.125 MHz at 100 MIPS)
 *   0  wait Timer0       10 ms        PTGT0LIM = 10 ms
 *   1  loop C0 -> 0      x25 = 250 ms PTGC0LIM = 24 (25 passes)
 *   2  trigger output 24 toggle LED
 *   3  interrupt PTG0    (CPU counts)
 *   4  jump to 0
 *
 * Details of every step and register are in the PTG.h tab.
 * NOT VERIFIED: the MPLAB simulator does not model the PTG (it never leaves
 * step 0), and the board has not been built yet. The step encodings come from
 * Microchip's MCC device data for this chip. '?' and 'l' show the step pointer,
 * so on a real chip you can see at once whether the PTG is running.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "PTG.h"

const unsigned long BAUD = 115200;
const uint8_t RP_LED = 61;                  /* RC13 */
const uint8_t RP_D9 = 36;                   /* RB4  */
const uint8_t RP_BUTTON = 60;               /* RC12 */

volatile unsigned long irq0 = 0, irq1 = 0;
extern "C" void __attribute__((interrupt, no_auto_psv)) _PTG0Interrupt(void) { irq0++; _PTG0IF = 0; }
extern "C" void __attribute__((interrupt, no_auto_psv)) _PTG1Interrupt(void) { irq1++; _PTG1IF = 0; }

/* ---------------- the four programs ---------------- */
const uint8_t PROG1[] = {               /* blink 2 Hz */
    PTG_WAIT_T0,                        /* 0: 10 ms */
    PTG_JMPC0(0),                       /* 1: 25 x -> 250 ms */
    PTG_TRIG(PTG_OUT_PPS24),            /* 2: toggle LED */
    PTG_IRQ(0),                         /* 3: tell the CPU */
    PTG_JMP(0),                         /* 4 */
};
const uint8_t PROG2[] = {               /* button -> 5 flashes */
    PTG_WLO(PTG_IN_INT2),               /* 0: wait for USER pressed (high -> low) */
    PTG_TRIG(PTG_OUT_PPS24),            /* 1: toggle LED */
    PTG_WAIT_T0,                        /* 2: 10 ms */
    PTG_JMPC0(2),                       /* 3: x10 = 100 ms */
    PTG_JMPC1(1),                       /* 4: x10 toggles = 5 flashes */
    PTG_IRQ(0),                         /* 5: burst done */
    PTG_JMP(0),                         /* 6 */
};
const uint8_t PROG3[] = {               /* software stepping */
    PTG_WAIT_SW_EDGE,                   /* 0: wait for the CPU ('g') */
    PTG_TRIG(PTG_OUT_PPS24),            /* 1: toggle LED */
    PTG_TRIG(PTG_OUT_PPS25),            /* 2: toggle D9 */
    PTG_IRQ(1),                         /* 3 */
    PTG_JMP(0),                         /* 4 */
};
const uint8_t PROG4[] = {               /* 1 kHz on D9 + 2 Hz LED */
    PTG_WAIT_T0,                        /* 0: 500 us */
    PTG_TRIG(PTG_OUT_PPS25),            /* 1: toggle D9 -> 1 kHz square wave */
    PTG_JMPC0(0),                       /* 2: x500 = 250 ms */
    PTG_TRIG(PTG_OUT_PPS24),            /* 3: toggle LED */
    PTG_IRQ(0),                         /* 4 */
    PTG_JMP(0),                         /* 5 */
};

const uint8_t *progSteps = 0;
uint8_t progLen = 0, progNum = 0;

void loadProgram(uint8_t n)
{
    ptgBegin(32);                           /* PTG clock = FCY / 32 */
    PTGCSTbits.PTGTOGL = 1;                 /* trigger steps toggle their output */
    switch (n) {
    case 1: ptgSetTimer0Us(10000); PTGC0LIM = 24; progSteps = PROG1; progLen = sizeof(PROG1); break;
    case 2: ptgSetTimer0Us(10000); PTGC0LIM = 9; PTGC1LIM = 9; progSteps = PROG2; progLen = sizeof(PROG2); break;
    case 3: progSteps = PROG3; progLen = sizeof(PROG3); break;
    case 4: ptgSetTimer0Us(500); PTGC0LIM = 499; progSteps = PROG4; progLen = sizeof(PROG4); break;
    }
    progNum = n;
    irq0 = irq1 = 0;
    ptgLoad(progSteps, progLen);
    ptgStart();
    Serial.print("# program "); Serial.print(n); Serial.println(" running");
}

void listProgram()
{
    Serial.print("# program "); Serial.print(progNum);
    Serial.print("   T0LIM="); Serial.print(PTGT0LIM); Serial.print(" ("); Serial.print((PTGT0LIM + 1UL) * 1000000UL / ptgTickHz);
    Serial.print(" us)  C0LIM="); Serial.print(PTGC0LIM); Serial.print("  C1LIM="); Serial.println(PTGC1LIM);
    for (uint8_t i = 0; i < progLen; i++) {
        Serial.print(i == ptgStep() ? " >" : "  "); Serial.print(i); Serial.print("  0x");
        if (progSteps[i] < 16) Serial.print('0');
        Serial.print(progSteps[i], HEX); Serial.print("  "); ptgPrintStep(progSteps[i]); Serial.println();
    }
}

void status()
{
    Serial.print("# PTG program "); Serial.print(progNum);
    Serial.print("  step="); Serial.print(ptgStep());
    Serial.print("  busy="); Serial.print(ptgBusy());
    Serial.print("  irq0="); Serial.print(irq0);
    Serial.print("  irq1="); Serial.print(irq1);
    Serial.print("  PTGCST=0x"); Serial.println(PTGCST, HEX);
}

void busyTest()
{
    Serial.println("# interrupts OFF, CPU busy for 3 s. The LED keeps its rhythm (programs 1 and 4).");
    Serial.flush();
    unsigned long before = irq0;
    noInterrupts();
    for (volatile unsigned long i = 0; i < FCY / 4; i++) { }    /* ~3 s of pure CPU work */
    interrupts();
    Serial.print("# done. The PTG kept going; its interrupt flag waited for the CPU (irq0 +");
    Serial.print(irq0 - before); Serial.println(" now)");
}

void help()
{
    Serial.println();
    Serial.println("Lab 33 - Peripheral Trigger Generator");
    Serial.println("  1 blink  2 button burst  3 software step (g)  4 1 kHz on D9  b busy test  l list  ? status");
}

void setup()
{
    Serial.begin(BAUD);
    pinMode(LED_BUILTIN, OUTPUT);           /* TRIS = output; the PTG drives the level */
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(9, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);

    ptgOutputToPin(RP_LED, PPS_CODE_PTGTRG24);
    ptgOutputToPin(RP_D9, PPS_CODE_PTGTRG25);
    __builtin_write_RPCON(0x0000);
    _INT2R = RP_BUTTON;                     /* INT2 input = USER button (PTG input 15) */
    __builtin_write_RPCON(0x0800);

    _PTG0IF = 0; _PTG0IP = 2; _PTG0IE = 1;
    _PTG1IF = 0; _PTG1IP = 2; _PTG1IE = 1;

    help();
    loadProgram(1);
}

unsigned long lastPrint = 0, lastIrq0 = 0, lastIrq1 = 0;

void loop()
{
    /* report what the PTG did - the CPU only watches */
    if (irq0 != lastIrq0 || irq1 != lastIrq1) {
        if (millis() - lastPrint >= 1000 || progNum == 2 || progNum == 3) {
            lastPrint = millis();
            lastIrq0 = irq0; lastIrq1 = irq1;
            if (progNum == 2) { Serial.print("# burst "); Serial.print(irq0); Serial.println(" done"); }
            else if (progNum == 3) { Serial.print("# software step "); Serial.print(irq1); Serial.println(": LED and D9 toggled"); }
            else { Serial.print("# LED toggles so far: "); Serial.println(irq0); }
        }
    }

    if (!Serial.available()) return;
    char c = Serial.read();
    switch (c) {
    case '1': case '2': case '3': case '4': loadProgram(c - '0'); listProgram(); break;
    case 'g':
        if (progNum != 3) Serial.println("# 'g' is for program 3 (press 3 first)");
        ptgSoftTrigger();
        break;
    case 'b': busyTest(); break;
    case 'l': listProgram(); break;
    case '?': status(); break;
    default: break;
    }
}
