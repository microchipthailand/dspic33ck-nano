/*
 * Lab 19 - RTOS: a tiny preemptive real-time kernel, written from scratch
 * Board: dsPIC33CK Nano
 *
 * Goal:  see what an RTOS (FreeRTOS, Zephyr, ...) really does inside:
 *          - several tasks, each with its OWN STACK, running "at the same time"
 *          - preemption: a 1 ms tick interrupt switches tasks by force
 *          - priorities: the highest-priority READY task always runs
 *          - rtosDelay(): a task sleeps, the CPU goes to someone else
 *          - a semaphore: an interrupt wakes a waiting task (button press)
 *          - a lock so two tasks can share Serial
 *          - a CPU-load measurement from the idle task
 *
 * The whole kernel is in the second tab, MiniRTOS.h (read it - the context
 * switch is explained there). This file only has the application:
 *
 *   void myTask(void) { for (;;) { ...; rtosDelay(100); } }   a task never returns
 *   rtosCreate(myTask, 2, "mine  ");                           in setup()
 *   rtosStart();                                               last line of setup()
 *
 * Tasks in this demo (priority: higher number = more important):
 *   3  tButton   waits on a semaphore given by the USER-button interrupt
 *   2  tBlink    blinks the USER LED every 250 ms (rtosDelay)
 *   1  tWorker   counts primes. Light mode (default): works 20 ms, sleeps 80 ms.
 *                Heavy mode ('w'): never sleeps -> 100 % load, and it shares
 *                the CPU round-robin with tReport (same priority)
 *   1  tReport   prints task statistics once a second, reads Serial
 *   0  idle      added by rtosStart(): runs only when nothing else can
 *
 * Hardware: nothing to wire. Serial Monitor on J5 at 115200.
 *   Press USER: tButton reacts immediately even while tWorker is busy - that
 *   is preemption + priorities. Type 'w' to switch the worker between light
 *   (~20 % CPU) and heavy (100 % CPU) and watch "load" and "ran" change.
 *
 * Resources: the kernel borrows SCCP2 (the PWM of D6) as its 1 ms tick and the
 * unused CRC interrupt vector for the context switch.
 */

#include <Arduino.h>
#include "MiniRTOS.h"

/* ---------------- shared data ---------------- */
Sem buttonSem;                             /* given by the button interrupt */
volatile bool workerHeavy = false;
volatile uint32_t primes = 0, presses = 0;

/* USER button interrupt: wake tButton */
void onButton() { presses++; semGiveFromISR(buttonSem); }

/* ---------------- the tasks ---------------- */

/* priority 2: blink the LED, 50 ms on, 200 ms off */
void tBlink(void)
{
    for (;;) {
        digitalWrite(LED_BUILTIN, LED_ON);  rtosDelay(50);
        digitalWrite(LED_BUILTIN, LED_OFF); rtosDelay(200);
    }
}

/* priority 3: sleep until the button is pressed, then report it */
void tButton(void)
{
    for (;;) {
        semTake(buttonSem);                /* sleeps until the ISR gives it */
        uint32_t t = rtosTicks();
        lockSerial();
        Serial.print("[button] press #"); Serial.print(presses);
        Serial.print(" handled at tick "); Serial.println(t);
        unlockSerial();
    }
}

/* priority 1: busy work - count primes the slow way */
void tWorker(void)
{
    uint32_t n = 3;
    uint32_t burstStart = rtosTicks();
    for (;;) {
        bool prime = true;
        for (uint32_t d = 3; d * d <= n; d += 2) if (n % d == 0) { prime = false; break; }
        if (prime) primes++;
        n += 2;
        /* no yield inside the burst: only the tick can take the CPU away */
        if (!workerHeavy && rtosTicks() - burstStart >= 20) {
            rtosDelay(80);                 /* light mode: 20 ms on, 80 ms off */
            burstStart = rtosTicks();
        }
    }
}

/* priority 1: once a second print the statistics; 'w' toggles the worker */
void tReport(void)
{
    for (;;) {
        rtosDelay(1000);
        int load = rtosCpuLoad();

        lockSerial();
        Serial.print("t="); Serial.print(rtosTicks());
        Serial.print(" ms  switches="); Serial.print(switches);
        Serial.print("  primes="); Serial.print(primes);
        Serial.print("  load="); Serial.print(load);
        Serial.print("%  worker="); Serial.println(workerHeavy ? "HEAVY" : "light");
        rtosPrintTasks();
        unlockSerial();

        while (Serial.available() > 0)
            if (Serial.read() == 'w') workerHeavy = !workerHeavy;
    }
}

/* ---------------- start ---------------- */
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 19 - mini preemptive RTOS. Press USER; type w = worker light/heavy");

    semInit(buttonSem, 0);
    rtosCreate(tBlink,  2, "blink ");
    rtosCreate(tButton, 3, "button");
    rtosCreate(tWorker, 1, "worker");
    rtosCreate(tReport, 1, "report");
    attachInterrupt(digitalPinToInterrupt(BUTTON_BUILTIN), onButton, FALLING);

    rtosStart();                           /* never returns */
}

void loop() { }                            /* not used: the kernel owns the CPU */
