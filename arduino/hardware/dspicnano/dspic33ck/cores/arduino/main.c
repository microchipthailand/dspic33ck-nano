/*
 * main.c - Arduino entry point for dsPIC33CK
 *
 * This file provides the standard Arduino startup sequence:
 *   1. System initialization (clock, config bits)
 *   2. Arduino core init (timers, peripherals)
 *   3. Call user's setup()
 *   4. Repeatedly call user's loop()
 *
 * The user writes setup() and loop() in their .ino sketch,
 * which gets compiled as C by XC16.
 */

#include "Arduino.h"
#include "system_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int main(void)
{
    /* Initialize dsPIC33CK system (clock, config bits) */
    system_config_init();

    /* Initialize Arduino core (Timer1 for millis, etc.) */
    init();

    /* C++ global constructors have ALREADY run at this point: XC-DSC's crt0
     * calls every entry of the .ctors table (__ctor) before main(). The link
     * uses --gc-sections, so the table is kept by ldscripts/keep_ctors.ld
     * (see platform.txt). As on other Arduino boards they run BEFORE
     * system_config_init() and init(): a constructor must only store values,
     * and leave hardware set-up to begin(). */

    /* Call user's setup function */
    setup();

    /* Main loop */
    while (1) {
        loop();
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
