/*
 * Lab 19 - RTOS: a tiny preemptive real-time kernel, written from scratch
 * Board: dsPIC33CK Nano
 *
 * Goal:  see what an RTOS (FreeRTOS, Zephyr, ...) really does inside, by
 *        building one in ~250 lines:
 *          - 4 tasks, each with its OWN STACK, running "at the same time"
 *          - preemption: a 1 ms tick interrupt switches tasks by force
 *          - priorities: the highest-priority READY task always runs
 *          - rtosDelay(): a task sleeps, the CPU goes to someone else
 *          - a semaphore: an interrupt wakes a waiting task (button press)
 *          - a mutex-like lock so two tasks can share Serial
 *          - a CPU-load measurement from the idle task
 *
 * Tasks in this demo (priority: higher number = more important):
 *   3  tButton   blocks on a semaphore given by the USER-button interrupt
 *   2  tBlink    blinks the USER LED every 250 ms (rtosDelay)
 *   1  tReport   prints task statistics once a second
 *   1  tWorker   counts primes. Light mode (default): works 20 ms, sleeps 80 ms.
 *                Heavy mode ('w'): never sleeps -> 100 % load, and it shares
 *                the CPU round-robin with tReport (same priority)
 *   0  idle      runs only when nothing else can = the free CPU time
 *
 * Hardware: nothing to wire. Serial Monitor on J5 at 115200.
 *   Press USER: tButton reacts immediately even while tWorker is busy - that
 *   is preemption + priorities. Type 'w' to switch the worker between light
 *   (~20 % CPU) and heavy (100 % CPU) and watch "load" and "ran" change.
 *
 * Resources: borrows SCCP2 (the PWM of D6) as the 1 ms kernel tick, and the
 * unused CRC interrupt vector as the "context switch" software interrupt.
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

#include <Arduino.h>

/* ============================ kernel ============================ */
#define MAX_TASKS   5
#define STACK_WORDS 256                    /* 512 bytes per task */

enum { READY = 0, SLEEPING = 1, BLOCKED = 2 };

struct TCB {
    volatile uint16_t sp;                  /* saved W15 - MUST be the first member */
    uint16_t splim;                        /* stack limit for this task            */
    uint8_t  prio, state;
    volatile uint32_t wakeTick;
    volatile uint32_t runTicks;            /* ticks this task was running          */
    const char *name;
};

TCB  tasks[MAX_TASKS];
uint16_t stacks[MAX_TASKS][STACK_WORDS];
uint8_t  nTasks = 0;

extern "C" {
    volatile TCB *currentTCB;              /* used by the assembly below */
    volatile TCB *nextTCB;
}
volatile uint8_t  current = 0;
volatile uint32_t kTicks = 0;
volatile uint32_t switches = 0;

/* forward */
void rtosYield(void);

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

/* The context switch - the heart of the kernel. Written in assembly because
 * it must touch W15 and save registers the C compiler does not know about.
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

static void taskExit(void) { for (;;) rtosYield(); }   /* tasks should never return */

int rtosCreate(void (*fn)(void), uint8_t prio, const char *name)
{
    if (nTasks >= MAX_TASKS) return -1;
    uint8_t id = nTasks;
    uint16_t *s = stacks[id];
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
    tasks[id].splim    = (uint16_t)(&s[STACK_WORDS - 8]);   /* leave room for a trap frame */
    tasks[id].prio     = prio;
    tasks[id].state    = READY;
    tasks[id].wakeTick = 0;
    tasks[id].runTicks = 0;
    tasks[id].name     = name;
    nTasks++;
    return id;
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

/* --- counting semaphore --- */
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

/* --- a simple lock for Serial (spin + yield) --- */
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

void rtosStart(void)
{
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

/* ============================ application ============================ */
Sem buttonSem;
volatile bool workerHeavy = false;
volatile uint32_t idleCount = 0, primes = 0, presses = 0;

void onButton() { presses++; semGiveFromISR(buttonSem); }

void tIdle(void)
{
    for (;;) idleCount++;
}

void tBlink(void)
{
    for (;;) {
        digitalWrite(LED_BUILTIN, LED_ON);  rtosDelay(50);
        digitalWrite(LED_BUILTIN, LED_OFF); rtosDelay(200);
    }
}

void tButton(void)
{
    for (;;) {
        semTake(buttonSem);                /* sleeps until the ISR gives it */
        uint32_t t = kTicks;
        lockSerial();
        Serial.print("[button] press #"); Serial.print(presses);
        Serial.print(" handled at tick "); Serial.println(t);
        unlockSerial();
    }
}

void tWorker(void)
{
    uint32_t n = 3;
    uint32_t burstStart = kTicks;
    for (;;) {
        bool prime = true;                 /* deliberately slow trial division */
        for (uint32_t d = 3; d * d <= n; d += 2) if (n % d == 0) { prime = false; break; }
        if (prime) primes++;
        n += 2;
        /* no yield inside the burst: only the tick can take the CPU away */
        if (!workerHeavy && kTicks - burstStart >= 20) {
            rtosDelay(80);                 /* light mode: 20 ms on, 80 ms off */
            burstStart = kTicks;
        }
    }
}

void tReport(void)
{
    uint32_t lastIdleTicks = 0, lastTick = 0;
    for (;;) {
        rtosDelay(1000);
        /* CPU load = share of the last second's ticks NOT spent in the idle task */
        uint32_t idleT = tasks[0].runTicks;
        uint32_t di = idleT - lastIdleTicks; lastIdleTicks = idleT;
        uint32_t dt = kTicks - lastTick;     lastTick = kTicks;
        int load = dt ? (int)(100UL - 100UL * di / dt) : 0;

        lockSerial();
        Serial.print("t="); Serial.print(kTicks);
        Serial.print(" ms  switches="); Serial.print(switches);
        Serial.print("  primes="); Serial.print(primes);
        Serial.print("  load="); Serial.print(load);
        Serial.print("%  worker="); Serial.println(workerHeavy ? "HEAVY" : "light");
        for (uint8_t i = 0; i < nTasks; i++) {
            Serial.print("   "); Serial.print(tasks[i].name);
            Serial.print(" prio "); Serial.print(tasks[i].prio);
            Serial.print(tasks[i].state == READY ? " READY   " : tasks[i].state == SLEEPING ? " SLEEPING" : " BLOCKED ");
            Serial.print(" ran "); Serial.print(tasks[i].runTicks); Serial.print(" ms");
            uint16_t used = 0;             /* stack high-water mark: first non-0xA5A5 from the top */
            for (int w = STACK_WORDS - 1; w >= 0; w--) if (stacks[i][w] != 0xA5A5) { used = (uint16_t)(w + 1); break; }
            Serial.print("  stack "); Serial.print(used * 2); Serial.print("/"); Serial.println(STACK_WORDS * 2);
        }
        unlockSerial();

        while (Serial.available() > 0) {
            if (Serial.read() == 'w') { workerHeavy = !workerHeavy; }
        }
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 19 - mini preemptive RTOS. Press USER; type w = worker light/heavy");

    for (int i = 0; i < MAX_TASKS; i++)    /* paint stacks to measure usage later */
        for (int w = 0; w < STACK_WORDS; w++) stacks[i][w] = 0xA5A5;

    semInit(buttonSem, 0);
    rtosCreate(tIdle,   0, "idle  ");
    rtosCreate(tBlink,  2, "blink ");
    rtosCreate(tButton, 3, "button");
    rtosCreate(tWorker, 1, "worker");
    rtosCreate(tReport, 1, "report");
    attachInterrupt(digitalPinToInterrupt(BUTTON_BUILTIN), onButton, FALLING);

    rtosStart();                           /* never returns */
}

void loop() { }                            /* not used: the kernel owns the CPU */
