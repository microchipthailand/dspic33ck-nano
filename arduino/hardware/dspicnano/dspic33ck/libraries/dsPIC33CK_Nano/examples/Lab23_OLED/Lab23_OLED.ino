/*
 * Lab 23 - OLED display (SSD1306 128x64, I2C) - a graphics driver from scratch
 * Board: dsPIC33CK Nano
 *
 * Goal:  drive the most common hobby display without any library: initialise
 *        the SSD1306 controller, keep a frame buffer in RAM, draw pixels, lines,
 *        rectangles and text with your own 5x7 font, and push the buffer over
 *        I2C. The demo is a small dashboard: live pot voltage as a big number,
 *        a bar, a scrolling graph, uptime, and a button counter.
 *
 * Parts: 0.96" SSD1306 OLED module, 128x64, I2C (4 pins: GND VCC SCL SDA).
 *        (The 128x32 version also works - set OLED_H to 32.)
 *        10k potentiometer (optional, same as Lab 04).
 *
 * Wiring (breadboard):
 *   OLED GND -> GND (J3-2)
 *   OLED VCC -> 3V3 (J3-1)
 *   OLED SCL -> RB8 = SCL1 (J3-8)
 *   OLED SDA -> RB9 = SDA1 (J3-7)
 *   Pot wiper -> A0 = RA0 (J2-15), ends to 3V3 / GND
 *   Modules have 4.7k-10k pull-ups on board. Run Lab 08 first: it should
 *   find the display at 0x3C (some modules: 0x3D - change OLED_ADDR).
 *
 * USER button: next screen (dashboard -> graph -> text/font page)
 *
 * HOW IT WORKS
 *   SSD1306 memory: 128 columns x 8 "pages"; each page is 8 pixel rows, one
 *   byte per column, bit 0 = top row. So pixel (x, y) is bit (y & 7) of byte
 *   buf[(y / 8) * 128 + x]. The whole screen = 1024 bytes of RAM.
 *   I2C protocol: after the address, a CONTROL byte says what follows:
 *     0x00 = command bytes,  0x40 = display data bytes.
 *   This Wire library buffers 32 bytes per transmission, so the 1024-byte
 *   frame goes out in chunks of 1 control + 16 data bytes (64 transactions).
 *   At 400 kHz one full frame takes about 1024 * 9 bits * 1.07 / 400k = ~25 ms.
 *   Init sequence (from the SSD1306 datasheet "software configuration" flow):
 *     display off, clock div, multiplex = H-1, offset 0, start line 0,
 *     charge pump ON (0x8D,0x14 - required when the module runs from 3.3 V),
 *     horizontal addressing mode, segment remap + COM scan direction
 *     (= not mirrored), COM pins, contrast, pre-charge, VCOMH, display on.
 */

#include <Arduino.h>
#include <Wire.h>

#define OLED_ADDR 0x3C
#define OLED_W    128
#define OLED_H    64
#define CHUNK     16                       /* data bytes per I2C transaction (< 32) */

uint8_t fb[OLED_W * OLED_H / 8];           /* the frame buffer */

/* ---------------- 5x7 font, ASCII 32..127, column bytes, bit0 = top ---------------- */
static const uint8_t FONT[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08},{0x7F,0x7F,0x7F,0x7F,0x7F},
};

/* ---------------- low level: commands and data over I2C ---------------- */
bool oledCmd(uint8_t c)
{
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);                      /* control byte: command */
    Wire.write(c);
    return Wire.endTransmission() == 0;
}

void oledCmds(const uint8_t *c, int n) { for (int i = 0; i < n; i++) oledCmd(c[i]); }

bool oledBegin(void)
{
    static const uint8_t init[] = {
        0xAE,                    /* display OFF                                   */
        0xD5, 0x80,              /* clock divide ratio / oscillator frequency     */
        0xA8, OLED_H - 1,        /* multiplex ratio = number of rows - 1          */
        0xD3, 0x00,              /* display offset 0                              */
        0x40,                    /* start line 0                                  */
        0x8D, 0x14,              /* charge pump ON (needed at 3.3 V)              */
        0x20, 0x00,              /* memory mode: horizontal addressing            */
        0xA1,                    /* segment remap: column 127 = SEG0              */
        0xC8,                    /* COM scan direction: remapped (not mirrored)   */
        0xDA, (OLED_H == 64) ? 0x12 : 0x02,   /* COM pins configuration            */
        0x81, 0xCF,              /* contrast                                      */
        0xD9, 0xF1,              /* pre-charge period                             */
        0xDB, 0x40,              /* VCOMH deselect level                          */
        0xA4,                    /* display follows RAM                           */
        0xA6,                    /* normal (not inverted)                         */
        0xAF,                    /* display ON                                    */
    };
    Wire.beginTransmission(OLED_ADDR);
    if (Wire.endTransmission() != 0) return false;   /* nobody answered (no ACK) */
    oledCmds(init, sizeof(init));
    return true;
}

/* send the whole frame buffer: set the window to the full screen, then data */
void oledShow(void)
{
    static const uint8_t win[] = { 0x21, 0, OLED_W - 1,  0x22, 0, OLED_H / 8 - 1 };
    oledCmds(win, sizeof(win));
    for (unsigned i = 0; i < sizeof(fb); i += CHUNK) {
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x40);                  /* control byte: data */
        for (int k = 0; k < CHUNK; k++) Wire.write(fb[i + k]);
        Wire.endTransmission();
    }
}

/* ---------------- graphics on the frame buffer ---------------- */
void clear(void) { memset(fb, 0, sizeof(fb)); }

void pixel(int x, int y, bool on = true)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t m = (uint8_t)(1u << (y & 7));
    if (on) fb[(y >> 3) * OLED_W + x] |= m; else fb[(y >> 3) * OLED_W + x] &= (uint8_t)~m;
}

void line(int x0, int y0, int x1, int y1)          /* Bresenham */
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void rect(int x, int y, int w, int h, bool fill = false)
{
    if (fill) { for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) pixel(x + j, y + i); return; }
    line(x, y, x + w - 1, y); line(x, y + h - 1, x + w - 1, y + h - 1);
    line(x, y, x, y + h - 1); line(x + w - 1, y, x + w - 1, y + h - 1);
}

/* character with integer scaling: size 1 = 6x8 cell, 2 = 12x16, 3 = 18x24 */
int drawChar(int x, int y, char c, int size = 1)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *g = FONT[c - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] & (1 << row))
                for (int a = 0; a < size; a++) for (int b = 0; b < size; b++)
                    pixel(x + col * size + a, y + row * size + b);
    return x + 6 * size;
}

int text(int x, int y, const char *s, int size = 1)
{
    while (*s) x = drawChar(x, y, *s++, size);
    return x;
}

/* ---------------- demo screens ---------------- */
#define GRAPH_N 128
uint8_t graph[GRAPH_N];                    /* history of the pot, 0..47 */
uint8_t gHead = 0;
int screen = 0;
unsigned long presses = 0, frames = 0, frameMs = 0;

void fmtVolts(char *buf, int raw)          /* "2.47" without printf/float formatting */
{
    long mv = (long)raw * 3300L / 1023L;
    buf[0] = (char)('0' + mv / 1000); buf[1] = '.';
    buf[2] = (char)('0' + (mv / 100) % 10); buf[3] = (char)('0' + (mv / 10) % 10); buf[4] = 0;
}

void utoa10(unsigned long v, char *b)
{
    char t[11]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    int k = 0; while (i) b[k++] = t[--i]; b[k] = 0;
}

void drawDashboard(int raw)
{
    char b[16];
    text(0, 0, "dsPIC33CK Nano", 1);
    line(0, 9, 127, 9);
    fmtVolts(b, raw);
    int x = text(4, 16, b, 3);             /* big number */
    text(x + 2, 30, "V", 2);
    rect(0, 44, 128, 9);                   /* bar */
    rect(2, 46, (int)(124L * raw / 1023L), 5, true);
    text(0, 56, "up ", 1);
    utoa10(millis() / 1000, b); x = text(18, 56, b, 1); text(x, 56, "s", 1);
    text(70, 56, "btn ", 1); utoa10(presses, b); text(94, 56, b, 1);
}

void drawGraph(void)
{
    text(0, 0, "A0 history", 1);
    rect(0, 10, 128, 54);
    for (int i = 1; i < GRAPH_N - 2; i++) {
        /* gHead = oldest sample; walk forward through the 128-entry ring */
        int a = graph[(gHead + i) % GRAPH_N], c = graph[(gHead + i + 1) % GRAPH_N];
        line(i, 62 - a, i + 1, 62 - c);
    }
}

void drawFontPage(void)
{
    char b[12];
    text(0, 0, "Font 5x7, size 1:", 1);
    int x = 0, y = 10;
    for (char c = 33; c < 127; c++) {
        x = drawChar(x, y, c, 1);
        if (x > 122) { x = 0; y += 9; }
    }
    text(0, 46, "Size 2", 2);
    text(80, 56, "fps ", 1);
    utoa10(frameMs ? 1000UL / frameMs : 0, b); text(104, 56, b, 1);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);                 /* SSD1306 supports 400 kHz fast mode */

    Serial.println("Lab 23 - SSD1306 OLED on I2C (SDA RB9 J3-7, SCL RB8 J3-8)");
    if (!oledBegin()) {
        Serial.println("# no OLED at 0x3C - check wiring, run Lab 08, try OLED_ADDR 0x3D");
        for (;;) { digitalWrite(LED_BUILTIN, (millis() / 100) % 2 ? LED_ON : LED_OFF); }
    }
    Serial.println("# OLED found. USER = next screen");
    clear();
    text(16, 20, "Hello!", 2);
    text(10, 44, "Lab 23 - OLED", 1);
    oledShow();
    delay(1000);
}

void loop()
{
    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) { screen = (screen + 1) % 3; presses++; delay(30); }
    lastBtn = btn;

    int raw = analogRead(A0);
    graph[gHead] = (uint8_t)(raw * 47L / 1023L);     /* ring buffer of 128 samples */
    gHead = (uint8_t)((gHead + 1) % GRAPH_N);

    unsigned long t0 = millis();
    clear();
    if (screen == 0) drawDashboard(raw);
    else if (screen == 1) drawGraph();
    else drawFontPage();
    oledShow();
    frameMs = millis() - t0;
    frames++;

    digitalWrite(LED_BUILTIN, (frames & 8) ? LED_ON : LED_OFF);
    if (frames % 50 == 0) { Serial.print("# frame time "); Serial.print(frameMs); Serial.println(" ms"); }
}
