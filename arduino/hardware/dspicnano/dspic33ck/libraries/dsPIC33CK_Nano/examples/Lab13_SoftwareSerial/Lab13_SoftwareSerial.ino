/*
 * Lab 13 - Software serial (a UART made from a timer + two GPIO pins)
 * Board: dsPIC33CK Nano
 *
 * Goal:  understand what a UART actually does by building one in software:
 *        start bit, 8 data bits LSB first, stop bit, sampled by a timer ISR.
 *        Then use it as a bridge: PC <-> Serial (J5) <-> SoftSerial <-> device.
 *
 * Pins (any two digital pins work - even PORTA, which has no PPS):
 *   SoftSerial TX = RA3 = D3 (J2-18)
 *   SoftSerial RX = RA4 = D4 (J2-19)
 *
 * Test 1 (no extra parts): jumper J2-18 to J2-19 (TX -> RX loopback).
 *   Type in the Serial Monitor: text goes out of the software TX, comes back
 *   in the software RX, and is echoed with "soft rx:".
 * Test 2: remove the jumper, connect a second USB-serial adapter or another
 *   board: its RX -> J2-18, its TX -> J2-19, GND -> GND. Open a terminal on it
 *   at 9600 baud: you now have a two-way bridge.
 *
 * How it works:
 *   - Timer SCCP3 interrupts at 3 x baud (28.8 kHz for 9600).
 *   - TX: every 3rd tick shifts out the next bit (start 0, data, stop 1).
 *   - RX: idle-high line; when a 0 is seen, wait 1 tick so we are near the
 *     MIDDLE of the start bit, then sample every 3 ticks = centre of each bit.
 *   - Both directions are full-duplex inside the same ISR.
 * Limits: 9600 baud is safe. 19200 works at 100 MIPS. A hardware UART
 *   (Serial / Serial2 in Lab 21) is always the better choice - it costs no CPU.
 * Resources: uses SCCP3, which is the PWM of D7 - no analogWrite(7) here.
 */

#include <Arduino.h>

const uint8_t SOFT_TX = 3;     /* RA3 */
const uint8_t SOFT_RX = 4;     /* RA4 */
const unsigned long SOFT_BAUD = 9600;

/* ---------------- the software UART ---------------- */
#define SS_RXBUF 64
static volatile uint8_t  ssRx[SS_RXBUF];
static volatile uint8_t  ssHead = 0, ssTail = 0;
static volatile uint16_t ssTxFrame = 0;  /* bits still to send, LSB first      */
static volatile uint8_t  ssTxBits = 0;   /* how many bits left in ssTxFrame    */
static volatile uint8_t  ssTxTick = 0;
static volatile uint8_t  ssRxState = 0;  /* 0 idle, 1..8 data bit, 9 stop bit  */
static volatile uint8_t  ssRxTick = 0;
static volatile uint8_t  ssRxByte = 0;
static volatile unsigned long ssFrameErrors = 0;

/* fast pin access for the ISR (resolved once, like the core's tone()) */
static volatile uint16_t *ssTxLat;  static uint16_t ssTxMask;
static volatile uint16_t *ssRxPort; static uint16_t ssRxMask;

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT3Interrupt(void)
{
    IFS2bits.CCT3IF = 0;

    /* ---- transmit: one bit every 3 ticks ---- */
    if (ssTxBits) {
        if (++ssTxTick >= 3) {
            ssTxTick = 0;
            if (ssTxFrame & 1) *ssTxLat |= ssTxMask; else *ssTxLat &= (uint16_t)~ssTxMask;
            ssTxFrame >>= 1;
            ssTxBits--;
        }
    }

    /* ---- receive ---- */
    bool level = (*ssRxPort & ssRxMask) != 0;
    if (ssRxState == 0) {
        if (!level) { ssRxState = 1; ssRxTick = 4; ssRxByte = 0; } /* start bit seen: 1 tick + 3 = middle of bit 0 */
    } else if (--ssRxTick == 0) {
        ssRxTick = 3;
        if (ssRxState <= 8) {                         /* data bits, LSB first */
            ssRxByte >>= 1;
            if (level) ssRxByte |= 0x80;
            ssRxState++;
        } else {                                      /* stop bit */
            if (level) {
                uint8_t next = (uint8_t)((ssHead + 1) % SS_RXBUF);
                if (next != ssTail) { ssRx[ssHead] = ssRxByte; ssHead = next; }
            } else {
                ssFrameErrors++;                      /* stop bit was 0: noise / wrong baud */
            }
            ssRxState = 0;
        }
    }
}

void softSerialBegin(unsigned long baud)
{
    pinMode(SOFT_RX, INPUT_PULLUP);                   /* idle high if unconnected */
    digitalWrite(SOFT_TX, HIGH);
    pinMode(SOFT_TX, OUTPUT);

    ssTxLat  = g_pin_map[SOFT_TX].lat_reg;  ssTxMask = (uint16_t)(1u << g_pin_map[SOFT_TX].bit);
    ssRxPort = g_pin_map[SOFT_RX].port_reg; ssRxMask = (uint16_t)(1u << g_pin_map[SOFT_RX].bit);

    /* SCCP3 as a plain 16-bit timer, clock FCY, prescaler 1:1, period = FCY / (3*baud) */
    unsigned long period = FCY / (3UL * baud);
    CCP3CON1L = 0;                       /* off while configuring */
    CCP3CON1H = 0; CCP3CON2L = 0; CCP3CON2H = 0; CCP3CON3H = 0;
    CCP3TMRL = 0; CCP3TMRH = 0;
    CCP3PRL = (uint16_t)(period - 1);
    CCP3PRH = 0xFFFF;
    IPC9bits.CCT3IP = 6;                 /* above millis() (4) so bit timing stays tight */
    IFS2bits.CCT3IF = 0;
    IEC2bits.CCT3IE = 1;
    CCP3CON1L = 0x8000;                  /* CCPON, MOD=0000 timer, CLKSEL=FOSC/2, TMRPS 1:1 */
}

void softSerialWrite(uint8_t c)
{
    while (ssTxBits) { }                 /* wait for the previous frame */
    noInterrupts();
    ssTxFrame = (uint16_t)((uint16_t)c << 1) | 0x200;   /* start 0, 8 data, stop 1 */
    ssTxTick = 2;                        /* first bit goes out on the next tick */
    ssTxBits = 10;
    interrupts();
}

int softSerialAvailable(void) { return (SS_RXBUF + ssHead - ssTail) % SS_RXBUF; }

int softSerialRead(void)
{
    if (ssHead == ssTail) return -1;
    uint8_t c = ssRx[ssTail];
    ssTail = (uint8_t)((ssTail + 1) % SS_RXBUF);
    return c;
}

void softSerialPrint(const char *s) { while (*s) softSerialWrite((uint8_t)*s++); }

/* ---------------- demo: bridge ---------------- */
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    softSerialBegin(SOFT_BAUD);
    Serial.println("Lab 13 - software serial, TX RA3 (J2-18), RX RA4 (J2-19), 9600 baud");
    Serial.println("Jumper J2-18 to J2-19 and type something.");
    softSerialPrint("Hello from the dsPIC33CK Nano software UART\r\n");
}

void loop()
{
    /* PC -> software TX */
    while (Serial.available() > 0) {
        softSerialWrite((uint8_t)Serial.read());
    }

    /* software RX -> PC */
    static bool atLineStart = true;
    while (softSerialAvailable() > 0) {
        int c = softSerialRead();
        if (atLineStart) { Serial.print("soft rx: "); atLineStart = false; }
        Serial.write((uint8_t)c);
        if (c == '\n') atLineStart = true;
        digitalWrite(LED_BUILTIN, LED_ON);
    }

    static unsigned long lastReport = 0;
    if (millis() - lastReport > 5000) {
        lastReport = millis();
        digitalWrite(LED_BUILTIN, LED_OFF);
        if (ssFrameErrors) {
            Serial.print("\r\n# framing errors: ");
            Serial.println(ssFrameErrors);
        }
    }
}
