/*
 * system_config.h - dsPIC33CK system configuration
 *
 * Handles clock setup and configuration bits.
 * This isolates Microchip-specific initialization from the Arduino API.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <xc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System initialization */
void system_config_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CONFIG_H */
