/*
 * Lab 10 - Pin-change interrupts: Change Notification on many pins + external INT1
 * Board: dsPIC33CK Nano
 *
 * Goal:  react to signals on MANY pins without polling them:
 *   1. attachInterrupt() on 4 pins with RISING / FALLING / CHANGE
 *      -> the core uses the dsPIC "Change Notification" (CN) hardware: every
 *         pin of PORTA..PORTD can interrupt, both edges, no PPS needed
 *   2. a ROTARY ENCODER read with pin-change interrupts (quadrature decoding)
 *   3. the dedicated EXTERNAL INTERRUPT INT1 programmed directly
 *      (one pin through PPS, one edge, fixed priority, very low latency)
 *   4. an edge counter + pulse-width meter: how fast can a CN interrupt follow?
 *
 * Arduino UNO comparison: "pin change interrupts" (PCINT0..2, one vector per
 * port) and "external interrupts" (INT0/INT1). The dsPIC has exactly the same
 * two kinds - CN (one vector per port, any pin) and INT0..INT3 (dedicated).
 *
 * Wiring (breadboard):
 *   USER button (on board, RC12)                  : attachInterrupt FALLING
 *   push button 2: D21 = RC0 (J2-14) -> GND       : attachInterrupt CHANGE (internal pull-up)
 *   rotary encoder A -> D22 = RC1 (J2-20), B -> D23 = RC2 (J2-21), C -> GND
 *   test signal input: D24 = RC3 (J3-22)          : edge counter (RISING)
 *   INT1 input:        D15 = RB10 (J2-4)          : external interrupt INT1 (falling)
 *   Generate a test signal without a generator: jumper D9 = RB4 (J3-15) to
 *   D24 = RC3 (J3-22) and send 't' - the sketch makes a square wave on D9
 *   with tone(), and counts its edges on RC3.
 *   No encoder? Turn it into two buttons, or just watch the other channels.
 *
 * Serial commands: t <Hz>  test tone on D9 (e.g. "t 1000")   s stop   ? status
 *
 * ----------------------------------------------------------------------------
 * HOW IT WORKS
 *   Change Notification (CN), per port x = A..D:
 *     CNCONx  ON (bit 15) + CNSTYLE (bit 11) = edge-detect style
 *     CNEN0x  enable "positive edge" per pin,  CNEN1x  "negative edge" per pin
 *     CNFx    which pin(s) changed (flag per pin, cleared by writing 0)
 *     one interrupt vector per PORT (_CNAInterrupt ... _CNDInterrupt); the ISR
 *     reads CNFx to see which pin fired.
 *   The core's attachInterrupt() does all of that and calls your function,
 *   so in a sketch you never touch CN registers - and you MUST NOT define
 *   _CNxInterrupt yourself (the core owns those vectors).
 *   INT1 (this lab, register level):
 *     RPINR0.INT1R = RP number of the pin (PPS input)
 *     INTCON2.INT1EP = 1 falling / 0 rising edge
 *     IFS0.INT1IF, IEC0.INT1IE, IPC3.INT1IP;  vector _INT1Interrupt
 *   Rotary encoder: on every edge of A or B, the 2-bit state (A,B) moves
 *     00 -> 01 -> 11 -> 10 (one way) or the reverse. A 16-entry table
 *     (old state << 2 | new state) gives +1 / -1 / 0 (0 = bounce/invalid).
 *   Debounce in an ISR: ignore edges closer than N ms (buttons), but NOT for
 *     the encoder (the table already rejects invalid transitions) and not for
 *     the test signal (we want every edge).
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>

const uint8_t PIN_BTN2 = 21;   /* RC0 */
const uint8_t PIN_ENCA = 22;   /* RC1 */
const uint8_t PIN_ENCB = 23;   /* RC2 */
const uint8_t PIN_SIG  = 24;   /* RC3 */
const uint8_t PIN_INT1 = 15;   /* RB10 */
const uint8_t PIN_TONE = 9;    /* RB4 */

volatile unsigned long userPresses = 0, btn2Changes = 0, sigEdges = 0, int1Count = 0;
volatile unsigned long lastUserMs = 0, lastBtn2Ms = 0;
volatile long encPos = 0;
volatile uint8_t encState = 0;
volatile unsigned long encErrors = 0;
volatile unsigned long sigLastUs = 0, sigMinUs = 0xFFFFFFFFUL;

/* ---------------- handlers called by the core's CN interrupt ---------------- */
void onUser()                          /* FALLING on RC12 */
{
    unsigned long now = millis();
    if (now - lastUserMs > 40) { userPresses++; lastUserMs = now; }   /* debounce */
}

void onBtn2()                          /* CHANGE on RC0: both press and release */
{
    unsigned long now = millis();
    if (now - lastBtn2Ms > 20) { btn2Changes++; lastBtn2Ms = now; }
}

/* quadrature table: index = old(2 bits) << 2 | new(2 bits) */
static const int8_t QDEC[16] = { 0, +1, -1, 0,  -1, 0, 0, +1,  +1, 0, 0, -1,  0, -1, +1, 0 };

void onEncoder()                       /* CHANGE on A and on B */
{
    uint8_t s = (uint8_t)((digitalRead(PIN_ENCA) << 1) | digitalRead(PIN_ENCB));
    int8_t d = QDEC[(encState << 2) | s];
    if (d == 0 && s != encState) encErrors++;      /* skipped a state: too fast or bounce */
    encPos += d;
    encState = s;
}

void onSignal()                        /* RISING on RC3 */
{
    unsigned long now = micros();
    unsigned long dt = now - sigLastUs;
    if (sigEdges && dt < sigMinUs) sigMinUs = dt;
    sigLastUs = now;
    sigEdges++;
}

/* ---------------- INT1, register level ---------------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _INT1Interrupt(void)
{
    IFS0bits.INT1IF = 0;
    int1Count++;
}

void int1Begin(uint8_t pin, bool falling)
{
    pinMode(pin, INPUT_PULLUP);                    /* also makes the pin digital */
    int16_t rp = pinToRP(pin);
    __builtin_write_RPCON(0x0000);                 /* unlock PPS */
    RPINR0bits.INT1R = (uint16_t)rp;               /* INT1 input <- RPn */
    __builtin_write_RPCON(0x0800);                 /* lock PPS */
    INTCON2bits.INT1EP = falling ? 1 : 0;          /* edge polarity */
    IPC3bits.INT1IP = 5;                           /* above the CN handlers (2) */
    IFS0bits.INT1IF = 0;
    IEC0bits.INT1IE = 1;
}

/* ---------------- demo ---------------- */
long readNumber(void)
{
    long v = 0; unsigned long t0 = millis();
    while (millis() - t0 < 60) {
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c == ' ') { Serial.read(); continue; }
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0'); t0 = millis();
        }
    }
    return v;
}

void status(void)
{
    noInterrupts();
    unsigned long u = userPresses, b = btn2Changes, e = sigEdges, i1 = int1Count, ee = encErrors, mn = sigMinUs;
    long p = encPos;
    interrupts();
    Serial.print("USER="); Serial.print(u);
    Serial.print("  btn2(change)="); Serial.print(b);
    Serial.print("  encoder="); Serial.print(p);
    Serial.print(" (bad steps "); Serial.print(ee);
    Serial.print(")  INT1="); Serial.print(i1);
    Serial.print("  RC3 edges="); Serial.print(e);
    if (e > 1) { Serial.print(" (min period "); Serial.print(mn); Serial.print(" us)"); }
    Serial.println();
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);                /* external 10k pull-up on the board */
    pinMode(PIN_BTN2, INPUT_PULLUP);
    pinMode(PIN_ENCA, INPUT_PULLUP);
    pinMode(PIN_ENCB, INPUT_PULLUP);
    pinMode(PIN_SIG, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 10 - pin-change interrupts: CN on RC12/RC0/RC1/RC2/RC3 + INT1 on RB10. t <Hz>, s, ?");

    encState = (uint8_t)((digitalRead(PIN_ENCA) << 1) | digitalRead(PIN_ENCB));
    attachInterrupt(digitalPinToInterrupt(BUTTON_BUILTIN), onUser, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_BTN2), onBtn2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCA), onEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCB), onEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_SIG), onSignal, RISING);
    int1Begin(PIN_INT1, true);
}

void loop()
{
    static unsigned long last = 0;
    static unsigned long seen = 0;
    noInterrupts(); unsigned long total = userPresses + btn2Changes + int1Count + (unsigned long)encPos; interrupts();
    if (total != seen || millis() - last >= 2000) {    /* print on every event, or every 2 s */
        seen = total; last = millis();
        status();
    }
    digitalWrite(LED_BUILTIN, (encPos & 1) ? LED_ON : LED_OFF);

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 't') {
            long hz = readNumber();
            if (hz < 31) hz = 1000;
            noInterrupts(); sigEdges = 0; sigMinUs = 0xFFFFFFFFUL; interrupts();
            tone(PIN_TONE, (unsigned int)hz);
            Serial.print("# test signal "); Serial.print(hz);
            Serial.println(" Hz on D9 (J3-15) - jumper it to RC3 (J3-22)");
        } else if (c == 's') {
            noTone(PIN_TONE); Serial.println("# test signal off");
        } else if (c == '?') status();
    }
}
