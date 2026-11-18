/*
 * Lab 03 - Serial Monitor: print and receive commands
 * Board: dsPIC33CK Nano
 *
 * Goal:  talk to the PC over the UART header J5.
 *
 * Wiring: a 3.3 V USB-serial adapter (CP2102, CH340, FT232 set to 3.3 V) on J5:
 *   J5-1 3V3   (leave unconnected if the board is powered from USB-C)
 *   J5-2 TX    -> adapter RX     (RC10, MCU transmit)
 *   J5-3 RX    <- adapter TX     (RC11, MCU receive)
 *   J5-4 GND   -- adapter GND
 * Serial Monitor: 115200 baud, line ending "Newline".
 *
 * Commands:  1 = LED on   0 = LED off   ? = status
 */

#include <Arduino.h>

unsigned long counter = 0;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    Serial.begin(115200);
    Serial.println();
    Serial.println("Lab 03 - dsPIC33CK Nano says hello!");
    Serial.println("Send 1 (LED on), 0 (LED off) or ? (status)");
}

void loop()
{
    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == '1') {
            digitalWrite(LED_BUILTIN, LED_ON);
            Serial.println("LED ON");
        } else if (c == '0') {
            digitalWrite(LED_BUILTIN, LED_OFF);
            Serial.println("LED OFF");
        } else if (c == '?') {
            Serial.print("uptime ms = ");
            Serial.println(millis());
            Serial.print("button    = ");
            Serial.println(digitalRead(BUTTON_BUILTIN) == LOW ? "pressed" : "released");
        }
    }

    static unsigned long last = 0;
    if (millis() - last >= 2000) {     /* heartbeat every 2 s */
        last = millis();
        Serial.print("tick ");
        Serial.println(counter++);
    }
}
