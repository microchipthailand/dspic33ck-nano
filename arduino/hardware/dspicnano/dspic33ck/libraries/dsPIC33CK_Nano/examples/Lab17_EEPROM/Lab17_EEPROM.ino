/*
 * Lab 17 - EEPROM emulation in Flash (data that survives power-off)
 * Board: dsPIC33CK Nano
 *
 * Goal:  the dsPIC33CK has NO data EEPROM. Real products store settings in
 *        program Flash instead. Learn how: erase pages, double-word writes,
 *        the NVMKEY unlock, and a log that spreads the wear.
 *
 * All the Flash code is in the second tab, FlashEEPROM.h (read it - the
 * "HOW IT WORKS" notes are there). It gives you 256 bytes of EEPROM:
 *
 *   EEPbegin();                     once, in setup()
 *   EEPwrite(address, data);        store one byte    (address 0..255, data 0..255)
 *   EEPread(address)                read one byte     (never written = 255)
 *   EEPput(address, variable);      store a whole variable or struct
 *   EEPget(address, variable);      read it back
 *   EEPclear();   EEPdump();        erase everything / print all 256 bytes
 *
 * Hardware: nothing to wire. USB-serial adapter on J5 (115200).
 *
 * Memory map used by this sketch:
 *   address 0..7    struct Settings, with EEPput / EEPget
 *                     magic 0x5A = "settings were saved before"
 *                     bootCount  incremented on every reset / power-up
 *                     blinkMs    LED blink period
 *                     userValue  any number you type
 *   address 16..255 free: try the w / r commands on them
 *
 * Serial commands:
 *   ?            show the settings
 *   b<ms>        set the blink period, e.g. b250        (saved at once)
 *   v<num>       set the user value,   e.g. v1234       (saved at once)
 *   w<addr> <n>  EEPwrite one byte,    e.g. w20 123
 *   r<addr>      EEPread one byte,     e.g. r20
 *   d            dump all 256 bytes
 *   f            factory reset (erase everything)
 *   USER button: userValue + 1 and save
 *
 * Power-cycle the board: bootCount keeps counting, blinkMs is remembered,
 * and the bytes you wrote with w are still there.
 * Any NEW UPLOAD erases the EEPROM (the programmer and the Serial bootloader
 * both erase the whole application area).
 */

#include <Arduino.h>
#include "FlashEEPROM.h"

/* ---------------- the settings, stored at address 0 ---------------- */
const int     SETTINGS_ADDR = 0;
const uint8_t MAGIC = 0x5A;

struct Settings {
    uint8_t  magic;
    uint8_t  spare;
    uint16_t bootCount;
    uint16_t blinkMs;
    uint16_t userValue;
};                                      /* 8 bytes: addresses 0..7 */
Settings cfg;

bool saveSettings(void) { return EEPput(SETTINGS_ADDR, cfg); }

/* ---------------- demo ---------------- */
void printSettings(void)
{
    Serial.print("bootCount="); Serial.print(cfg.bootCount);
    Serial.print("  blinkMs="); Serial.print(cfg.blinkMs);
    Serial.print("  userValue="); Serial.println(cfg.userValue);
    Serial.print("  page "); Serial.print(eepActivePage);
    Serial.print("  slot "); Serial.print(eepNextSlot); Serial.print("/"); Serial.print(EEP_SLOTS);
    Serial.print("  Flash writes this run "); Serial.println(eepWrites);
}

/* reads a number after a command letter, e.g. the "250" of "b250" */
long readNumber(void)
{
    long v = 0;
    bool digits = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 50) {               /* digits arrive back-to-back */
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c == ' ' || c == ',' || c == '=') {
                if (digits) break;             /* separator after the number: done */
                Serial.read(); continue;       /* separator before it: skip */
            }
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0');
            digits = true;
            t0 = millis();
        }
    }
    return digits ? v : -1;                    /* -1 = no number typed */
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 17 - EEPROM emulation in Flash.  ? b<ms> v<num> w<addr> <n> r<addr> d f");

    EEPbegin();
    EEPget(SETTINGS_ADDR, cfg);
    if (cfg.magic != MAGIC) {                  /* never saved: all bytes read 255 */
        Serial.println("# no settings saved yet - first start, using defaults");
        cfg.magic = MAGIC; cfg.spare = 0;
        cfg.bootCount = 0; cfg.blinkMs = 500; cfg.userValue = 0;
    }
    cfg.bootCount++;
    Serial.println(saveSettings() ? "# boot counted and saved" : "# SAVE FAILED");
    printSettings();
}

void loop()
{
    digitalWrite(LED_BUILTIN, (millis() / cfg.blinkMs) % 2 ? LED_ON : LED_OFF);

    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        cfg.userValue++;
        Serial.println(saveSettings() ? "# userValue + 1, saved (button)" : "# SAVE FAILED");
        printSettings();
        delay(30);
    }
    lastBtn = btn;

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == '?') printSettings();
        else if (c == 'd') EEPdump();
        else if (c == 'f') { EEPclear(); Serial.println("# EEPROM erased - reset to start over"); }
        else if (c == 'b') {
            long v = readNumber();
            if (v >= 20 && v <= 10000) { cfg.blinkMs = (uint16_t)v; Serial.println(saveSettings() ? "# saved" : "# SAVE FAILED"); }
            else Serial.println("# blink must be 20..10000 ms");
            printSettings();
        } else if (c == 'v') {
            long v = readNumber();
            if (v >= 0 && v <= 65535) { cfg.userValue = (uint16_t)v; Serial.println(saveSettings() ? "# saved" : "# SAVE FAILED"); }
            else Serial.println("# v<0..65535>, e.g. v1234");
            printSettings();
        } else if (c == 'w') {                 /* w<addr> <value> */
            long addr = readNumber();
            long val  = readNumber();
            if (addr < 0 || addr >= EEP_SIZE || val < 0 || val > 255) {
                Serial.println("# w<addr 0..255> <value 0..255>, e.g. w20 123");
            } else {
                if (addr < (long)sizeof(Settings)) Serial.println("# note: addresses 0..7 hold the settings");
                bool ok = EEPwrite((int)addr, (uint8_t)val);
                Serial.print(ok ? "# EEPwrite(" : "# WRITE FAILED (");
                Serial.print(addr); Serial.print(", "); Serial.print(val); Serial.println(")");
            }
        } else if (c == 'r') {                 /* r<addr> */
            long addr = readNumber();
            if (addr < 0 || addr >= EEP_SIZE) Serial.println("# r<addr 0..255>");
            else {
                Serial.print("# EEPread("); Serial.print(addr); Serial.print(") = ");
                Serial.println(EEPread((int)addr));
            }
        }
    }
}
