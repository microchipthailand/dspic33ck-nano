/*
 * PTG.h - Peripheral Trigger Generator driver for the dsPIC33CK
 *
 *   const uint8_t prog[] = { PTG_WAIT_T0, PTG_JMPC0(0), PTG_TRIG(24), PTG_JMP(0) };
 *   ptgBegin(32);                 // PTG clock = FCY / 32
 *   ptgSetTimer0Us(10000);        // Timer0 limit = 10 ms
 *   PTGC0LIM = 24;                // counter 0 limit
 *   ptgLoad(prog, sizeof(prog));
 *   ptgStart();
 *
 * ----------------------------------------------------------------------------
 * THE PTG
 *   A tiny processor with its own program of up to 32 one-byte STEPS. It can
 *   wait (for a timer, for an edge from a peripheral or a pin, for the CPU),
 *   count loops, jump, and fire triggers and interrupts. Once started it runs
 *   by itself: exact timing with zero CPU time, and it does not care what the
 *   CPU is doing (busy, interrupts off, debugger halted ...).
 *
 *   Resources: Timer0, Timer1 (16 bit, limits PTGT0LIM / PTGT1LIM),
 *   counter 0 and 1 (limits PTGC0LIM / PTGC1LIM), a step delay timer
 *   (PTGSDLIM), PTGHOLD / PTGADJ / PTGL0 to change limits while running.
 *
 * STEP = one byte: command in bits 7:4, option in bits 3:0
 *   0x0_  PTGCTRL   control, option below           0x1_ PTGADD / PTGCOPY
 *   0x2_  PTGSTRB   (0x20-0x3F) 5-bit literal -> ADCON3L (ADC channel select)
 *   0x4_  PTGWHI    wait for input n: low -> high   0x5_ PTGWLO  high -> low
 *   0x7_  PTGIRQ    PTG interrupt n (0-3)
 *   0x8_  PTGTRIG   (0x80-0x9F) trigger output n (0-31)
 *   0xA_  PTGJMP    (0xA0-0xBF) jump to step n
 *   0xC_  PTGJMPC0  (0xC0-0xDF) if C0 != C0LIM: C0++, jump to n; else C0 = 0, go on
 *   0xE_  PTGJMPC1  (0xE0-0xFF) the same with counter 1
 *   (encodings from Microchip's MCC device data for this chip)
 *
 * INPUTS on this chip (PTGWHI / PTGWLO n)
 *   0-3 PWM1-4 trigger   7 SCCP4   9 comparator 1   12 CLC1   13 ADC done
 *   15 INT2 pin (any pin, chosen with PPS: RPINR1.INT2R)
 * OUTPUTS (PTGTRIG n)
 *   12 ADC trigger   24, 25 to any pin via PPS (codes 46, 47)
 *   26, 27 to PPS inputs RP6 / RP7 (any peripheral input can listen)
 *   PTGCST.PTGTOGL = 1: every PTGTRIG toggles the output (a square wave)
 *                    0: every PTGTRIG is one pulse of PTGCON.PTGPWD clocks
 *
 * REGISTERS
 *   PTGCST  PTGEN on, PTGSTRT start, PTGSWT software trigger, PTGTOGL,
 *           PTGBUSY running, PTGWTO PTG watchdog time-out, PTGITM input mode
 *   PTGCON  PTGCLK clock (0 = FOSC/2 = FCY), PTGDIV divider 1..32, PTGPWD pulse width
 *   PTGQUE0..15  the steps, two per register (step 0 = low byte of PTGQUE0)
 *   PTGQPTR      which step runs now
 * ----------------------------------------------------------------------------
 */
#ifndef LAB33_PTG_H
#define LAB33_PTG_H

#include <Arduino.h>

/* ---------------- step encodings ---------------- */
#define PTG_NOP             0x00
#define PTG_SD_OFF          0x02    /* step delay timer off */
#define PTG_SD_ON           0x06    /* step delay timer on: PTGSDLIM after every step */
#define PTG_WAIT_T0         0x08    /* start Timer0, wait for PTGT0LIM */
#define PTG_WAIT_T1         0x09    /* start Timer1, wait for PTGT1LIM */
#define PTG_WAIT_SW         0x0A    /* wait while PTGSWT = 0 (level) */
#define PTG_WAIT_SW_EDGE    0x0B    /* wait for PTGSWT 0 -> 1 */
#define PTG_BROADCAST       0x0F    /* fire every output enabled in PTGBTE */
#define PTG_ADD_C0          0x10    /* PTGC0LIM += PTGADJ ... */
#define PTG_ADD_C1          0x11
#define PTG_ADD_T0          0x12
#define PTG_ADD_T1          0x13
#define PTG_ADD_SD          0x14
#define PTG_ADD_L0          0x15
#define PTG_COPY_C0         0x18    /* PTGC0LIM = PTGHOLD ... */
#define PTG_COPY_C1         0x19
#define PTG_COPY_T0         0x1A
#define PTG_COPY_T1         0x1B
#define PTG_COPY_SD         0x1C
#define PTG_COPY_L0         0x1D
#define PTG_STRB(v)         (0x20 | ((v) & 0x1F))
#define PTG_WHI(in)         (0x40 | ((in) & 0x0F))
#define PTG_WLO(in)         (0x50 | ((in) & 0x0F))
#define PTG_IRQ(n)          (0x70 | ((n) & 0x03))
#define PTG_TRIG(n)         (0x80 | ((n) & 0x1F))
#define PTG_JMP(s)          (0xA0 | ((s) & 0x1F))
#define PTG_JMPC0(s)        (0xC0 | ((s) & 0x1F))
#define PTG_JMPC1(s)        (0xE0 | ((s) & 0x1F))

/* inputs */
#define PTG_IN_PWM1         0
#define PTG_IN_SCCP4        7
#define PTG_IN_CMP1         9
#define PTG_IN_CLC1         12
#define PTG_IN_ADC          13
#define PTG_IN_INT2         15
/* outputs */
#define PTG_OUT_ADC         12
#define PTG_OUT_PPS24       24      /* PPS output code 46 */
#define PTG_OUT_PPS25       25      /* PPS output code 47 */
#define PPS_CODE_PTGTRG24   46
#define PPS_CODE_PTGTRG25   47

#define PTG_MAX_STEPS       32

/* ---------------- driver ---------------- */
unsigned long ptgTickHz = 0;                /* PTG clock after the divider */

void ptgStop()
{
    PTGCSTbits.PTGSTRT = 0;
    PTGCSTbits.PTGEN = 0;                   /* also resets the step pointer */
}

/* div = 1..32. PTG clock = FCY / div. */
void ptgBegin(uint8_t div)
{
    _PTGMD = 0;                             /* power the module (PMD7) */
    ptgStop();
    PTGCST = 0;
    PTGCON = 0;
    PTGCONbits.PTGCLK = 0;                  /* FOSC / 2 = FCY */
    PTGCONbits.PTGDIV = (div - 1) & 0x1F;
    PTGCONbits.PTGPWD = 3;                  /* 4-clock pulses (when PTGTOGL = 0) */
    ptgTickHz = FCY / div;
    PTGT0LIM = PTGT1LIM = PTGSDLIM = 0;
    PTGC0LIM = PTGC1LIM = 0;
    PTGHOLD = PTGADJ = PTGL0 = 0;
}

/* Timer limits in microseconds (16-bit: max 65535 PTG clocks) */
uint16_t ptgUs(unsigned long us)
{
    unsigned long t = (unsigned long)((unsigned long long)ptgTickHz * us / 1000000ULL);
    if (t > 65536UL) t = 65536UL;
    return t ? (uint16_t)(t - 1) : 0;
}
void ptgSetTimer0Us(unsigned long us) { PTGT0LIM = ptgUs(us); }
void ptgSetTimer1Us(unsigned long us) { PTGT1LIM = ptgUs(us); }

/* Copy the step program into PTGQUE0..15. Unused steps get PTG_NOP. */
void ptgLoad(const uint8_t *steps, uint8_t n)
{
    volatile uint8_t *q = (volatile uint8_t *)&PTGQUE0;
    for (uint8_t i = 0; i < PTG_MAX_STEPS; i++) q[i] = (i < n) ? steps[i] : PTG_NOP;
    PTGQPTR = 0;
}

void ptgStart()
{
    PTGCSTbits.PTGEN = 1;
    PTGCSTbits.PTGSTRT = 1;                 /* step 0 runs now */
}

/* A software trigger for PTG_WAIT_SW_EDGE: a 0 -> 1 edge on PTGSWT. */
void ptgSoftTrigger()
{
    PTGCSTbits.PTGSWT = 0;
    PTGCSTbits.PTGSWT = 1;
}

inline uint8_t ptgStep() { return PTGQPTR & 0x1F; }
inline bool ptgBusy()    { return PTGCSTbits.PTGBUSY; }

/* Map a PTG output (24 or 25) to a remappable pin, by RP number. */
void ptgOutputToPin(uint8_t rp, uint8_t ppsCode)
{
    volatile uint8_t *rpor = (volatile uint8_t *)&RPOR0;    /* RP32 = byte 0 */
    __builtin_write_RPCON(0x0000);
    rpor[rp - 32] = ppsCode;
    __builtin_write_RPCON(0x0800);
}

/* Print a step in words (for the 'l' listing) */
void ptgPrintStep(uint8_t s)
{
    uint8_t cmd = s >> 4, opt = s & 0x0F;
    switch (cmd) {
    case 0x0:
        switch (opt) {
        case 0x0: Serial.print("NOP"); break;
        case 0x2: Serial.print("step delay off"); break;
        case 0x6: Serial.print("step delay on"); break;
        case 0x8: Serial.print("wait Timer0"); break;
        case 0x9: Serial.print("wait Timer1"); break;
        case 0xA: Serial.print("wait software trigger (level)"); break;
        case 0xB: Serial.print("wait software trigger (edge)"); break;
        case 0xF: Serial.print("broadcast PTGBTE"); break;
        default:  Serial.print("PTGCTRL "); Serial.print(opt); break;
        }
        break;
    case 0x1: Serial.print(opt < 8 ? "PTGADD " : "PTGCOPY "); Serial.print(opt & 7); break;
    case 0x2: case 0x3: Serial.print("strobe ADC channel "); Serial.print(s & 0x1F); break;
    case 0x4: Serial.print("wait input "); Serial.print(opt); Serial.print(" low->high"); break;
    case 0x5: Serial.print("wait input "); Serial.print(opt); Serial.print(" high->low"); break;
    case 0x7: Serial.print("interrupt PTG"); Serial.print(opt & 3); break;
    case 0x8: case 0x9: Serial.print("trigger output "); Serial.print(s & 0x1F); break;
    case 0xA: case 0xB: Serial.print("jump to step "); Serial.print(s & 0x1F); break;
    case 0xC: case 0xD: Serial.print("loop C0 -> step "); Serial.print(s & 0x1F); break;
    case 0xE: case 0xF: Serial.print("loop C1 -> step "); Serial.print(s & 0x1F); break;
    default: Serial.print("? 0x"); Serial.print(s, HEX); break;
    }
}

#endif
