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

    /* C++ global constructors are called by crt0 before main().
     * The .ctors section is preserved (no --gc-sections in linker). */

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
