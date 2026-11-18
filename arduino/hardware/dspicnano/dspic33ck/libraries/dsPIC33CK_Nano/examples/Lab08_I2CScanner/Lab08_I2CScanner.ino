/*
 * Lab 08 - I2C bus scanner
 * Board: dsPIC33CK Nano
 *
 * Goal:  use the Wire library; find the address of any I2C module.
 *        Press USER to scan again.
 *
 * Wiring (breadboard) - I2C1, dedicated pins:
 *   SDA = RB9 = pin 14 (J3-7)     SCL = RB8 = pin 13 (J3-8)
 *   VCC = 3V3 (J3-1)              GND = J3-2
 *   Most breakout modules (BME280, MPU6050, SSD1306 OLED, 24LC EEPROM) already
 *   have pull-ups. For bare chips add 4.7k from SDA and SCL to 3V3.
 *   Use 3.3 V modules only - the I/O pins of this board are not 5 V tolerant.
 */

#include <Arduino.h>
#include <Wire.h>

void scan()
{
    int found = 0;
    Serial.println("Scanning I2C bus (0x08..0x77)...");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();   /* 0 = ACK */
        if (err == 0) {
            Serial.print("  device found at 0x");
            if (addr < 16) Serial.print("0");
            Serial.println((int)addr, HEX);
            found++;
        }
    }
    Serial.print("done, ");
    Serial.print(found);
    Serial.println(" device(s).");
    digitalWrite(LED_BUILTIN, found ? LED_ON : LED_OFF);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);   /* 100 kHz standard mode */
    Serial.println("Lab 08 - I2C scanner (SDA RB9, SCL RB8)");
    scan();
}

void loop()
{
    if (digitalRead(BUTTON_BUILTIN) == LOW) {
        scan();
        delay(300);
        while (digitalRead(BUTTON_BUILTIN) == LOW) { }
    }
}
