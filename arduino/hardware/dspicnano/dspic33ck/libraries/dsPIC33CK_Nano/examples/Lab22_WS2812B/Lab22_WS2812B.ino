/*
 * Lab 22 - WS2812B addressable RGB LEDs ("NeoPixel") driven by SPI
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required)
 *
 * Goal:  generate the WS2812B 800 kHz single-wire protocol with the SPI
 *        peripheral instead of fragile bit-banging, then animate a strip.
 *
 * Parts: WS2812B strip / ring / stick (this sketch: 8 LEDs, change NUM_LEDS)
 *        330 ohm series resistor, 470-1000 uF capacitor across the strip power.
 *
 * Wiring (breadboard):
 *   strip DIN -> 330 ohm -> MOSI = RC5 = D26 (J3-5)
 *   strip 5V  -> 5V (J2-1)   (max ~8 LEDs at full white from USB: 60 mA each)
 *   strip GND -> GND (J2-2)  - common ground is mandatory
 *   SCK (RC4, J3-6) toggles too but is not connected.
 *
 * 3.3 V data into a 5 V WS2812B: the datasheet asks for VIH >= 0.7 x VDD = 3.5 V.
 * Most strips work at 3.3 V anyway, especially with short wires; if yours
 * flickers, add a 74AHCT125 level shifter, or power the strip at ~4.5 V.
 *
 * HOW THE TIMING WORKS
 *   WS2812B bit = 1.25 us:  '0' = 0.40 us high + 0.85 us low
 *                           '1' = 0.80 us high + 0.45 us low   (+-150 ns)
 *   SPI at 3.333 MHz -> one SPI bit = 300 ns, so 4 SPI bits = 1.2 us = 1 LED bit:
 *     LED '0' -> SPI 1000  (300 ns high)
 *     LED '1' -> SPI 1110  (900 ns high)
 *   One LED byte (8 bits) = 32 SPI bits = 4 SPI bytes.
 *   The core's SPI clock: BRG = FCY/(2*f) - 1 = 100e6/(2*3.2e6) - 1 = 14 -> 3.333 MHz.
 *   At 4 MIPS the SPI cannot reach this speed, so the sketch refuses to run.
 *   >= 50 us low after the last bit = "latch" (show the colours).
 *   SPI.transfer() waits for each byte, so there is a short gap (a few hundred
 *   ns at 100 MIPS - estimated from the code, not scoped) between bytes. Every
 *   encoded byte ends with a 0 SPI bit, so the gap only lengthens a LOW phase,
 *   which WS2812B tolerates up to the ~5 us reset threshold.
 *
 * USER button: next animation.
 */

#include <Arduino.h>
#include <SPI.h>

#define NUM_LEDS   8
#define BRIGHTNESS 40          /* 0..255, keep low on USB power */

#if FCY < 50000000UL
#error "Lab22 needs Tools > Clock = 100 MIPS (SPI must run at 3.3 MHz)"
#endif

uint8_t leds[NUM_LEDS][3];     /* G, R, B - the WS2812B wire order */

/* two LED bits -> one SPI byte (each LED bit = 4 SPI bits) */
static const uint8_t PAIR[4] = { 0x88, 0x8E, 0xE8, 0xEE };   /* 00 01 10 11 */

void setPixel(int i, uint8_t r, uint8_t g, uint8_t b)
{
    leds[i][0] = (uint8_t)((g * BRIGHTNESS) >> 8);
    leds[i][1] = (uint8_t)((r * BRIGHTNESS) >> 8);
    leds[i][2] = (uint8_t)((b * BRIGHTNESS) >> 8);
}

void show(void)
{
    /* Pre-encode everything so the SPI writes are back-to-back. A gap longer
     * than ~5 us inside the stream would be taken as a reset by the strip. */
    static uint8_t buf[NUM_LEDS * 3 * 4];
    int k = 0;
    for (int i = 0; i < NUM_LEDS; i++)
        for (int c = 0; c < 3; c++) {
            uint8_t v = leds[i][c];
            buf[k++] = PAIR[(v >> 6) & 3];
            buf[k++] = PAIR[(v >> 4) & 3];
            buf[k++] = PAIR[(v >> 2) & 3];
            buf[k++] = PAIR[v & 3];
        }

    SPI.beginTransaction(SPISettings(3200000, MSBFIRST, SPI_MODE0));
    noInterrupts();            /* a millis() tick in the middle would stretch a bit */
    for (int j = 0; j < k; j++) SPI.transfer(buf[j]);
    interrupts();
    SPI.endTransaction();
    delayMicroseconds(80);     /* latch */
}

/* colour wheel 0..255 -> r,g,b */
void wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (pos < 85)       { *r = 255 - pos * 3; *g = pos * 3;       *b = 0; }
    else if (pos < 170) { pos -= 85;  *r = 0; *g = 255 - pos * 3; *b = pos * 3; }
    else                { pos -= 170; *r = pos * 3; *g = 0;       *b = 255 - pos * 3; }
}

int mode = 0;
const int MODES = 4;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    SPI.begin();
    pinMode(MOSI, OUTPUT);
    Serial.println("Lab 22 - WS2812B on MOSI RC5 (J3-5). USER = next effect");
    for (int i = 0; i < NUM_LEDS; i++) setPixel(i, 0, 0, 0);
    show();
}

void loop()
{
    static uint8_t t = 0;
    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        mode = (mode + 1) % MODES;
        Serial.print("effect ");
        Serial.println(mode);
        delay(30);
    }
    lastBtn = btn;

    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t r = 0, g = 0, b = 0;
        switch (mode) {
        case 0:  /* rainbow */
            wheel((uint8_t)(t + i * 256 / NUM_LEDS), &r, &g, &b);
            break;
        case 1:  /* running dot */
            if (i == (t / 8) % NUM_LEDS) { r = 255; g = 80; }
            break;
        case 2:  /* breathing blue */
            b = (t < 128) ? t * 2 : (255 - t) * 2;
            break;
        default: /* red / green / blue test - check colour order */
            if (i % 3 == 0) r = 255; else if (i % 3 == 1) g = 255; else b = 255;
            break;
        }
        setPixel(i, r, g, b);
    }
    show();
    t++;
    digitalWrite(LED_BUILTIN, (t & 32) ? LED_ON : LED_OFF);
    delay(20);
}
