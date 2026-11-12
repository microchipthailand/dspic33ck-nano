/*
 * Lab 09 - SPI loopback test
 * Board: dsPIC33CK Nano
 *
 * Goal:  use the SPI library. With MOSI wired to MISO every byte sent comes
 *        straight back, so you can test SPI with a single jumper wire.
 *
 * SPI1 pins on this board:
 *   SCK  = RC4 = pin 25 (J3-6)
 *   MOSI = RC5 = pin 26 (J3-5)
 *   MISO = RC8 = pin 29 (J3-14)
 *   SS   = RC9 = pin 30 (J3-13)  - software chip select
 *
 * Wiring: one jumper from J3-5 (MOSI) to J3-14 (MISO).
 * Result: LED on = loopback OK, LED blinking fast = mismatch / no jumper.
 * Next step: replace the jumper with a real SPI device (e.g. MCP3008 ADC,
 * 25LC EEPROM) and drive SS low around each transaction.
 */

#include <Arduino.h>
#include <SPI.h>

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
    Serial.begin(115200);
    SPI.begin();
    Serial.println("Lab 09 - SPI loopback (jumper MOSI J3-5 to MISO J3-14)");
}

void loop()
{
    int errors = 0;

    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(SS, LOW);
    for (int i = 0; i < 16; i++) {
        uint8_t tx = (uint8_t)(0xA5 ^ (i * 17));
        uint8_t rx = SPI.transfer(tx);
        if (rx != tx) errors++;
    }
    digitalWrite(SS, HIGH);
    SPI.endTransaction();

    Serial.print("16 bytes sent, errors = ");
    Serial.println(errors);

    if (errors == 0) {
        digitalWrite(LED_BUILTIN, LED_ON);
        delay(1000);
    } else {
        for (int k = 0; k < 10; k++) {
            digitalWrite(LED_BUILTIN, k % 2 ? LED_ON : LED_OFF);
            delay(100);
        }
    }
}
