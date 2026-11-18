/*
 * BoostMPPT - High-Resolution PWM for Boost Converter MPPT
 *
 * Demonstrates HRPWM library usage for a solar boost converter:
 *   - PG1 at 100kHz, complementary mode, 200ns dead-time
 *   - ADC trigger synchronized to mid-cycle for current sampling
 *   - Software duty sweep simulating P&O MPPT tracking
 *
 * Hardware:
 *   PWM1H = RB14 (D19) -> High-side gate driver
 *   PWM1L = RB15 (D20) -> Low-side gate driver (complementary)
 *
 * Board: dsPIC33CK256MP508 Curiosity (DM330030)
 */

#include <Arduino.h>
#include <HRPWM.h>

#define BOOST_CHANNEL   1           /* PG1: RB14/RB15 */
#define BOOST_FREQ      100000UL    /* 100 kHz switching frequency */
#define DEAD_TIME_NS    200         /* 200ns dead-time (rise and fall) */
#define DUTY_MIN        3277        /* ~5% of 65535 */
#define DUTY_MAX        58982       /* ~90% of 65535 */
#define DUTY_STEP       655         /* ~1% step */

static uint16_t currentDuty = 32768; /* Start at 50% */
static int16_t  dutyDirection = 1;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== HRPWM Boost MPPT Demo ===");
    Serial.println("PG1: 100kHz, Complementary, 200ns dead-time");
    Serial.println("");

    /* Initialize PG1 at 100kHz */
    HRPWM.begin(BOOST_CHANNEL, BOOST_FREQ);

    /* Complementary mode: PWM1H and PWM1L are inverted */
    HRPWM.setMode(BOOST_CHANNEL, HRPWM_COMPLEMENTARY);

    /* Center-aligned for lower EMI */
    HRPWM.setAlignment(BOOST_CHANNEL, HRPWM_CENTER_ALIGNED);

    /* Dead-time to prevent shoot-through */
    HRPWM.setDeadTime(BOOST_CHANNEL, DEAD_TIME_NS, DEAD_TIME_NS);

    /* ADC trigger at mid-cycle for current sampling */
    uint16_t midpoint = HRPWM.getPeriod(BOOST_CHANNEL) / 2;
    HRPWM.setADCTrigger(BOOST_CHANNEL, HRPWM_TRIGA, midpoint);

    /* Enable fault protection: drive both outputs LOW on fault */
    HRPWM.enableFault(BOOST_CHANNEL, HRPWM_FAULT_LOW);

    /* Set initial duty */
    HRPWM.duty(BOOST_CHANNEL, currentDuty);

    Serial.print("Period register = ");
    Serial.print_int((long)HRPWM.getPeriod(BOOST_CHANNEL), DEC);
    Serial.println(" ticks");
    Serial.println("Running... duty sweeps 5%-90% (simulating MPPT)");
    Serial.println("");

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    /* Simulate P&O MPPT: sweep duty up/down */
    currentDuty += dutyDirection * DUTY_STEP;

    if (currentDuty >= DUTY_MAX) {
        currentDuty = DUTY_MAX;
        dutyDirection = -1;
    } else if (currentDuty <= DUTY_MIN) {
        currentDuty = DUTY_MIN;
        dutyDirection = 1;
    }

    HRPWM.duty(BOOST_CHANNEL, currentDuty);

    /* Heartbeat LED */
    static uint8_t count = 0;
    static uint8_t ledState = 0;
    if (++count >= 100) {
        count = 0;
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);

        /* Print duty percentage */
        uint16_t pct = (uint16_t)((uint32_t)currentDuty * 100 / 65535);
        Serial.print("Duty: ");
        Serial.print_int((long)pct, DEC);
        Serial.println("%");
    }

    delay(10);
}
