/*
 * MiniRTOS.h - a tiny preemptive real-time kernel for Lab 19
 *
 * Everything the kernel does is in this file. The sketch only uses it:
 *
 *   rtosCreate(task, priority, "name");  add a task: void task(void) { for (;;) { ... } }
 *   rtosStart();                         start the kernel (never returns)
 *
 *   inside a task:
 *   rtosDelay(ms);                       sleep, the CPU goes to another task
 *   rtosYield();                         give the CPU to an equal-priority task
 *   rtosTicks()                          milliseconds since rtosStart()
 *
 *   Sem s;  semInit(s, 0);               counting semaphore
 *   semTake(s);                          wait until it is given
 *   semGiveFromISR(s);                   give it (also from an interrupt)
 *
 *   lockSerial();  ...  unlockSerial();  only one task prints at a time
 *
 *   rtosCpuLoad()                        % of CPU used since the last call
 *   rtosPrintTasks()                     table: name, priority, state, time, stack
 *
 * Priorities: higher number = more important. The highest-priority READY task
 * always runs; tasks with the same priority take turns every 1 ms tick.
 * Priority 0 belongs to the idle task, which rtosStart() adds by itself: it
 * runs only when nothing else can, and its run time is the free CPU time.
 *
 * Resources: SCCP2 (the PWM of D6, NanoTimer Timer2) is the 1 ms kernel tick,
 * and the unused CRC interrupt vector is the "context switch" interrupt.
 * Up to RTOS_MAX_TASKS - 1 = 5 tasks of your own, 512 bytes of stack each.
 *
 * ----------------------------------------------------------------------------
 * HOW A CONTEXT SWITCH WORKS ON A dsPIC33
 *   Every task has a stack. A task that is not running has ALL of its CPU
 *   registers saved on its own stack; the kernel only remembers its stack
 *   pointer (W15) in the task control block (TCB).
 *
 *   Switching = (1) push every register of the old task onto its stack,
 *               (2) save W15 into the old TCB,  (3) load W15 from the new TCB,
 *               (4) pop every register of the new task, (5) RETFIE.
 *   RETFIE pops PC and SR (incl. the CPU priority) that the interrupt entry
 *   pushed, so the new task resumes exactly where it was interrupted.
 *
 *   The switch runs in the LOWEST-priority interrupt (IPL1, the CRC vector,
 *   triggered by software: IFS3.CRCIF = 1). Like PendSV on ARM Cortex-M, this
 *   means it never interrupts another ISR - it runs after all of them are done.
 *
 *   Saved per task (see _CRCInterrupt below):
 *     W0-W14, RCOUNT, TBLPAG, DSRPAG, DSWPAG, CORCON, MODCON,
 *     ACCA (3 words), ACCB (3 words), DCOUNT, DOSTART, DOEND (the DO-loop),
 *     plus PC + SR from the interrupt itself.
 *   SPLIM (stack limit) is switched too, so a stack overflow in one task traps
 *   (StackError) instead of silently overwriting the next task.
 *
 *   A brand-new task gets a fake "interrupted" frame on its stack, so the
 *   first switch to it "returns" into the task's entry function.
 * ----------------------------------------------------------------------------
 */
#ifndef MINI_RTOS_H
#define MINI_RTOS_H

#include <Arduino.h>

#define RTOS_MAX_TASKS   6                 /* 5 of yours + the idle task */
#define RTOS_STACK_WORDS 256               /* 512 bytes per task */
#define RTOS_STACK_PAINT 0xA5A5            /* fill pattern, to measure stack use */

enum { READY = 0, SLEEPING = 1, BLOCKED = 2 };

struct TCB {
    volatile uint16_t sp;                  /* saved W15 - MUST be the first member */
    uint16_t splim;                        /* stack limit for this task            */
    uint8_t  prio, state;
    volatile uint32_t wakeTick;
    volatile uint32_t runTicks;            /* ticks this task was running          */
    const char *name;
};

TCB      tasks[RTOS_MAX_TASKS];
uint16_t stacks[RTOS_MAX_TASKS][RTOS_STACK_WORDS];
uint8_t  nTasks = 0;

extern "C" {
    volatile TCB *currentTCB;              /* used by the assembly below */
    volatile TCB *nextTCB;
}
volatile uint8_t  current = 0;
volatile uint32_t kTicks = 0;              /* 1 ms ticks since rtosStart() */
volatile uint32_t switches = 0;            /* context switches so far */
int8_t            idleId = -1;
volatile uint32_t idleCount = 0;           /* loops of the idle task */

void rtosYield(void);

uint32_t rtosTicks(void)
{
    uint32_t a, b;
    do { a = kTicks; b = kTicks; } while (a != b);   /* 32-bit read, not torn by the tick */
    return a;
}

/* ============================ scheduler ============================ */

/* Pick the highest-priority READY task; among equals take the next one after
 * the current task (round-robin). Called with interrupts at kernel level. */
static uint8_t schedule(void)
{
    int best = -1;
    for (uint8_t k = 1; k <= nTasks; k++) {
        uint8_t i = (uint8_t)((current + k) % nTasks);
        if (tasks[i].state == READY && (best < 0 || tasks[i].prio > tasks[best].prio)) best = i;
    }
    return (uint8_t)best;                  /* idle task is always READY */
}

static void requestSwitch(void)
{
    uint8_t n = schedule();
    if (n != current) {
        nextTCB = &tasks[n];
        current = n;
        switches++;
        IFS3bits.CRCIF = 1;                /* pend the context switch (lowest IPL) */
    }
}

/* 1 ms kernel tick: wake sleepers, account time, preempt */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT2Interrupt(void)
{
    IFS1bits.CCT2IF = 0;
    kTicks++;
    tasks[current].runTicks++;
    for (uint8_t i = 0; i < nTasks; i++)
        if (tasks[i].state == SLEEPING && (int32_t)(kTicks - tasks[i].wakeTick) >= 0)
            tasks[i].state = READY;
    requestSwitch();
}

/* ============================ context switch ============================ */
/* The heart of the kernel. Written in assembly because it must touch W15 and
 * save registers the C compiler does not know about.
 * Vector: _CRCInterrupt (CRC module is unused), priority 1. */
asm(
"    .section .isr.text._CRCInterrupt,code,keep\n"   /* 'keep': --gc-sections must not drop it */
"    .global __CRCInterrupt\n"
"__CRCInterrupt:\n"
"    bset.b  _SR, #5\n"                    /* CPU priority -> 7 for the whole   */
"    bset.b  _SR, #6\n"                    /* switch, so the tick cannot change */
"    bset.b  _SR, #7\n"                    /* nextTCB half-way through. RETFIE  */
                                           /* restores the task's own SR/IPL.   */
"    bclr.b  _IFS3, #3\n"                  /* CRCIF = 0 (bit 3 of IFS3)         */
/* ---- save the old task ---- */
"    push.d  w0\n"
"    push.d  w2\n"
"    push.d  w4\n"
"    push.d  w6\n"
"    push.d  w8\n"
"    push.d  w10\n"
"    push.d  w12\n"
"    push    w14\n"
"    push    _RCOUNT\n"
"    push    _TBLPAG\n"
"    push    _DSRPAG\n"
"    push    _DSWPAG\n"
"    push    _CORCON\n"
"    push    _MODCON\n"
"    push    _ACCAL\n"
"    push    _ACCAH\n"
"    push    _ACCAU\n"
"    push    _ACCBL\n"
"    push    _ACCBH\n"
"    push    _ACCBU\n"
"    push    _DCOUNT\n"
"    push    _DOSTARTL\n"
"    push    _DOSTARTH\n"
"    push    _DOENDL\n"
"    push    _DOENDH\n"
"    mov     _currentTCB, w0\n"
"    mov     w15, [w0]\n"                  /* currentTCB->sp = W15              */
/* ---- switch stacks: SPLIM must be valid for the NEW stack before W15 moves.
 *      First open SPLIM fully, move W15, then set the new task's limit. ---- */
"    mov     _nextTCB, w0\n"
"    mov     w0, _currentTCB\n"            /* currentTCB = nextTCB               */
"    setm    w1\n"
"    mov     w1, _SPLIM\n"                 /* SPLIM = 0xFFFF (no limit) ...      */
"    nop\n"                                /* (an SPLIM write needs 1 cycle      */
"    mov     [w0], w15\n"                  /*  before W15 may be used)           */
"    mov     [w0+2], w1\n"
"    mov     w1, _SPLIM\n"                 /* ... then this task's own limit     */
"    nop\n"
/* ---- restore the new task ---- */
"    pop     _DOENDH\n"
"    pop     _DOENDL\n"
"    pop     _DOSTARTH\n"
"    pop     _DOSTARTL\n"
"    pop     _DCOUNT\n"
"    pop     _ACCBU\n"
"    pop     _ACCBH\n"
"    pop     _ACCBL\n"
"    pop     _ACCAU\n"
"    pop     _ACCAH\n"
"    pop     _ACCAL\n"
"    pop     _MODCON\n"
"    pop     _CORCON\n"
"    pop     _DSWPAG\n"
"    pop     _DSRPAG\n"
"    pop     _TBLPAG\n"
"    pop     _RCOUNT\n"
"    pop     w14\n"
"    pop.d   w12\n"
"    pop.d   w10\n"
"    pop.d   w8\n"
"    pop.d   w6\n"
"    pop.d   w4\n"
"    pop.d   w2\n"
"    pop.d   w0\n"
"    retfie\n"
);

/* Number of words the switch pushes after the hardware PC/SR frame. */
#define CTX_WORDS (14 + 1 + 17)            /* w0..w13 (7 push.d), w14, 17 SFRs = 32 */

/* ============================ tasks ============================ */
static void taskExit(void) { for (;;) rtosYield(); }   /* tasks should never return */

static int rtosAddTask(void (*fn)(void), uint8_t prio, const char *name)
{
    if (nTasks >= RTOS_MAX_TASKS) return -1;
    uint8_t id = nTasks;
    uint16_t *s = stacks[id];
    for (int w = 0; w < RTOS_STACK_WORDS; w++) s[w] = RTOS_STACK_PAINT;   /* to measure use later */
    int k = 0;
    /* Small code model: a function pointer IS the 16-bit program address (or,
     * for code above 0xFFFF, the address of a GOTO handle in low memory). */
    uint32_t pc  = (uint16_t)fn;
    uint32_t ret = (uint16_t)taskExit;

    /* The stack grows UP on dsPIC. Build what an interrupt + our switch would
     * have left behind, from the bottom:
     *   [return address of fn = taskExit]  (as if fn had been CALLed)
     *   PC<15:0>, then SR<7:0>:IPL3:PC<22:16>  - the interrupt frame (RETFIE)
     *   32 words of saved registers, all 0 except the page/CORCON SFRs     */
    s[k++] = (uint16_t)ret;               /* CALL frame: PC low ...  */
    s[k++] = (uint16_t)(ret >> 16);       /* ... and PC high          */
    s[k++] = (uint16_t)pc;                /* RETFIE pops PC low        */
    s[k++] = (uint16_t)(pc >> 16) & 0x7F; /* SR<7:0>=0 (IPL0), IPL3=0, PC<22:16> */
    for (int i = 0; i < CTX_WORDS; i++) s[k++] = 0;
    /* CORCON slot: same value the C runtime uses (copy the live one) */
    s[4 + 14 + 1 + 4] = CORCON;           /* order: w0..w13,w14,RCOUNT,TBLPAG,DSRPAG,DSWPAG,CORCON */
    s[4 + 14 + 1 + 2] = DSRPAG;           /* PSV page as the runtime set it up */
    s[4 + 14 + 1 + 3] = DSWPAG;
    s[4 + 14 + 1 + 1] = TBLPAG;

    tasks[id].sp       = (uint16_t)(&s[k]);
    tasks[id].splim    = (uint16_t)(&s[RTOS_STACK_WORDS - 8]);   /* leave room for a trap frame */
    tasks[id].prio     = prio;
    tasks[id].state    = READY;
    tasks[id].wakeTick = 0;
    tasks[id].runTicks = 0;
    tasks[id].name     = name;
    nTasks++;
    return id;
}

/* add a task; returns its number, or -1 if there is no room
 * (the last slot is kept for the idle task) */
int rtosCreate(void (*fn)(void), uint8_t prio, const char *name)
{
    if (nTasks >= RTOS_MAX_TASKS - 1) return -1;
    return rtosAddTask(fn, prio, name);
}

void rtosYield(void)
{
    noInterrupts();
    requestSwitch();
    interrupts();                          /* the pended switch runs right here */
}

void rtosDelay(uint32_t ms)
{
    noInterrupts();
    tasks[current].wakeTick = kTicks + ms;
    tasks[current].state = SLEEPING;
    requestSwitch();
    interrupts();
}

/* ============================ semaphore ============================ */
struct Sem { volatile int16_t count; volatile int8_t waiter; };

void semInit(Sem &s, int16_t n) { s.count = n; s.waiter = -1; }

void semTake(Sem &s)                       /* block until the semaphore is given */
{
    noInterrupts();
    if (s.count > 0) { s.count--; interrupts(); return; }
    s.waiter = (int8_t)current;
    tasks[current].state = BLOCKED;
    requestSwitch();
    interrupts();                          /* sleeps here until semGive... */
}

void semGiveFromISR(Sem &s)                /* safe to call from an interrupt */
{
    if (s.waiter >= 0) {
        tasks[s.waiter].state = READY;
        s.waiter = -1;
        requestSwitch();                   /* higher prio task -> switch at ISR exit */
    } else if (s.count < 32767) s.count++;
}

/* ============================ Serial lock ============================ */
volatile int8_t serialOwner = -1;

void lockSerial(void)
{
    for (;;) {
        noInterrupts();
        if (serialOwner < 0) { serialOwner = (int8_t)current; interrupts(); return; }
        interrupts();
        rtosDelay(1);   /* NOT rtosYield(): a high-priority task that only yields
                         * would be picked again at once and the low-priority
                         * owner could never finish = priority-inversion deadlock */
    }
}
void unlockSerial(void) { serialOwner = -1; }

/* ============================ statistics ============================ */
/* bytes of stack a task has used so far (highest word that is not paint) */
uint16_t rtosStackUsed(uint8_t id)
{
    for (int w = RTOS_STACK_WORDS - 1; w >= 0; w--)
        if (stacks[id][w] != RTOS_STACK_PAINT) return (uint16_t)((w + 1) * 2);
    return 0;
}

/* CPU load in % since the last call = share of ticks NOT spent in idle */
int rtosCpuLoad(void)
{
    static uint32_t lastIdle = 0, lastTick = 0;
    uint32_t idleT = idleId >= 0 ? tasks[idleId].runTicks : 0;
    uint32_t now = rtosTicks();
    uint32_t di = idleT - lastIdle, dt = now - lastTick;
    lastIdle = idleT; lastTick = now;
    return dt ? (int)(100UL - 100UL * di / dt) : 0;
}

/* one line per task. Call it between lockSerial() and unlockSerial(). */
void rtosPrintTasks(void)
{
    for (uint8_t i = 0; i < nTasks; i++) {
        Serial.print("   "); Serial.print(tasks[i].name);
        Serial.print(" prio "); Serial.print(tasks[i].prio);
        Serial.print(tasks[i].state == READY ? " READY   " : tasks[i].state == SLEEPING ? " SLEEPING" : " BLOCKED ");
        Serial.print(" ran "); Serial.print(tasks[i].runTicks); Serial.print(" ms");
        Serial.print("  stack "); Serial.print(rtosStackUsed(i)); Serial.print("/"); Serial.println(RTOS_STACK_WORDS * 2);
    }
}

/* ============================ start ============================ */
static void rtosIdleTask(void) { for (;;) idleCount++; }

void rtosStart(void)
{
    idleId = (int8_t)rtosAddTask(rtosIdleTask, 0, "idle  ");  /* the slot kept free for it */

    /* Kernel tick: SCCP2 as a 16-bit timer at FCY, 1 ms period */
    PMD2bits.CCP2MD = 0;
    CCP2CON1L = 0; CCP2CON1H = 0; CCP2CON2L = 0; CCP2CON2H = 0; CCP2CON3H = 0;
    CCP2TMRL = 0; CCP2TMRH = 0;
    unsigned long per = FCY / 1000UL;      /* 100000 at 100 MIPS: needs prescaler */
    uint8_t ps = 0;
    while (per > 65535UL && ps < 3) { ps++; per = FCY / 1000UL / (ps == 1 ? 4 : ps == 2 ? 16 : 64); }
    CCP2CON1Lbits.TMRPS = ps;
    CCP2PRL = (uint16_t)(per - 1); CCP2PRH = 0xFFFF;
    IPC6bits.CCT2IP = 2;                   /* tick: above the switch, below millis/Serial */
    IFS1bits.CCT2IF = 0; IEC1bits.CCT2IE = 1;

    /* Context switch: CRC interrupt at the lowest priority */
    IPC12bits.CRCIP = 1;
    IFS3bits.CRCIF = 0; IEC3bits.CRCIE = 1;

    /* setup() is running on the main stack and becomes "nothing": the first
     * switch saves it into a dummy TCB that is never scheduled again. */
    static TCB bootTCB;
    currentTCB = &bootTCB;
    current = 0;
    uint8_t first = schedule();
    current = first;
    nextTCB = &tasks[first];
    CCP2CON1Lbits.CCPON = 1;
    IFS3bits.CRCIF = 1;                    /* go: switch into the first task */
    for (;;) { }                           /* never reached */
}

#endif /* MINI_RTOS_H */
