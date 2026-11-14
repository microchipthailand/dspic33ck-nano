/*
 * Lab 29 - Line follower robot: servo drive + Bluetooth (HC-05) remote
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  1. SERVO DRIVE: two continuous-rotation servos as wheels, driven by
 *           the SCCP hardware (NanoServo library), with a stop-point trim.
 *        2. LINE SENSING: a 5-channel IR reflectance array on A0..A4,
 *           calibrated, turned into one "where is the line" number.
 *        3. PD CONTROL: steer so the line stays under the middle sensor.
 *        4. BLUETOOTH: drive and tune the robot from a phone through an HC-05
 *           on Serial2. The same commands also work from the Serial Monitor.
 *
 * Hardware (see the wiring picture in the lab guide):
 *   Left  servo  signal -> D5 = RB0 (J3-21)     red -> battery +, brown -> GND
 *   Right servo  signal -> D6 = RB1 (J3-20)     red -> battery +, brown -> GND
 *   IR array (TCRT5000 x5, analog outputs), seen from above, robot going away:
 *       leftmost OUT1 -> A0 = RA0 (J2-15)   OUT2 -> A1 (J2-16)   OUT3 -> A2 (J2-17)
 *       OUT4 -> A3 (J2-18)   rightmost OUT5 -> A4 (J2-19)
 *       VCC -> 3V3 (J3-1)  GND -> GND.  NOT 5 V: RA0-RA4 are not 5 V tolerant.
 *   HC-05  VCC -> 5V (J2-1)  GND -> GND (J2-2)
 *          TXD -> D15 = RB10 (J2-4)    (= Serial2 RX)
 *          RXD <- D16 = RB11 (J2-5)    (= Serial2 TX, 3.3 V logic: no divider)
 *          STATE -> D17 = RB12 (J2-6)  (optional: HIGH while a phone is connected)
 *   Optional passive piezo on D9 = RB4 (J3-15): beeps, and the 'V' horn.
 *   POWER: 4 x AA (4.8-6 V) -> servo red wires AND J2-1 (+5V). The board's
 *          MCP1703 takes up to 16 V and its Schottky diode keeps the battery out
 *          of the USB cable, so USB-C can stay plugged in for the Serial
 *          Monitor. All grounds together. Never feed servos from USB alone:
 *          a stalled servo draws ~0.7 A.
 *   Serial Monitor on USB-serial J5: 115200, "Newline" or "Both NL & CR".
 *
 * Phone: pair "HC-05" (PIN 1234 or 0000), then use a Bluetooth terminal app
 * (e.g. "Serial Bluetooth Terminal") or an RC car app (e.g. "Bluetooth RC
 * Controller") - they send the single letters below.
 *
 * Commands - single characters act at once:
 *   F B L R   forward / back / spin left / spin right        S  stop
 *   G I H J   forward-left / forward-right / back-left / back-right
 *   0..9 q    manual speed 0 %..90 %, q = 100 %
 *   X / x     line following ON / OFF  (also: A = on, D = all stop)
 *   C         calibrate: put the middle sensor on the line first, the robot
 *             then turns left and right over it for 4 s
 *   V         horn (piezo on D9)               ?   status
 * Text commands (lower case, end with Enter):
 *   kp 0.9  kd 3  base 45   PD gains (2 decimals) and following speed in %
 *   trim l 1500  trim r 1500  stop point of each servo in us (find it with pulse)
 *   pulse l 1500 raw pulse to one servo, to find where it stands still
 *   dir l  dir r              reverse one wheel (servo mounted the other way)
 *   inv                       the line reads LOW instead of HIGH (C finds it too)
 *   dead 500                  manual drive stops if no command for 500 ms (0 = off)
 *   tel                       telemetry on / off (USB Serial Monitor, 5 per second)
 *   sensors                   raw and calibrated sensor values once
 *   help                      this list
 *   The USER button toggles line following without a phone.
 *
 * HC-05 SET-UP (name, PIN, baud): hold the USER button while pressing RESET.
 * The sketch then bridges Serial Monitor <-> HC-05 at 38400. Power the HC-05
 * up with its own button held (or EN/KEY to 3V3) to enter AT mode, set the
 * Serial Monitor to "Both NL & CR" and type:
 *   AT            -> OK
 *   AT+NAME=dsPIC-Robot      AT+PSWD="1234"   (older firmware: AT+PSWD=1234)
 *   AT+UART=9600,0,0         (this sketch talks to the HC-05 at 9600)
 *
 * ----------------------------------------------------------------------------
 * SERVO PULSES
 *   A hobby servo reads one pulse every 20 ms. Positional servo: 1.0..2.0 ms =
 *   one end..other end. Continuous-rotation servo: the same pulse is a SPEED,
 *   ~1.5 ms = stop, 1.0 ms = full speed one way, 2.0 ms = full the other way.
 *   Each servo stops at a slightly different width, so it gets a TRIM.
 *   The two wheels face opposite ways: "forward" is a longer pulse on one
 *   side and a shorter pulse on the other (dirL = +1, dirR = -1).
 *   NanoServo makes the pulses with an SCCP module each (dual-edge compare):
 *   the timer runs a 20 ms frame, the output goes high at 0 and low at RB.
 *   No interrupts, no jitter. detach() stops the pulses: a continuous servo
 *   then stands still whatever its trim is - that is how "S" stops.
 *
 * LINE POSITION
 *   Each sensor is scaled to 0..1000 between the darkest and brightest value
 *   seen during calibration (1000 = on the line). The position is the
 *   weighted average  sum(v[i] * i * 1000) / sum(v[i])  = 0 (line under the
 *   leftmost sensor) .. 4000 (rightmost), 2000 = centred. With two sensors
 *   partly on the line it lands in between, so the result is smooth, not
 *   just 5 steps.
 *   Line lost: keep turning to the side where it was last seen; after 1.5 s
 *   the robot gives up and stops.
 *
 * PD CONTROL (every 10 ms)
 *   error e = (position - 2000) / 20       -100 (line far left) .. +100
 *   turn    = kp * e + kd * (e - e_last)
 *   left wheel = base + turn,  right wheel = base - turn    (in %, -100..100)
 *   kp pulls the robot back to the line, kd damps the swinging. Start with
 *   kd = 0, raise kp until it follows but wobbles, then add kd until the
 *   wobble goes away. Gains are kept as integers x100 (no floating point).
 *
 * BLUETOOTH
 *   The HC-05 is a transparent serial link: what the phone sends comes out of
 *   its TXD pin, what we write to its RXD goes to the phone. So it is just
 *   Serial2 to the program. Writing is blocking (9600 baud = 1 ms per
 *   character), so the sketch keeps its Bluetooth replies short.
 *   STATE goes LOW when the phone disconnects: the robot stops.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <NanoServo.h>
#include <NanoSerial2.h>

/* ---------------- pins ---------------- */
const uint8_t PIN_SERVO_L  = 5;     /* RB0, J3-21 */
const uint8_t PIN_SERVO_R  = 6;     /* RB1, J3-20 */
const uint8_t PIN_BT_RX    = 15;    /* RB10, J2-4  <- HC-05 TXD */
const uint8_t PIN_BT_TX    = 16;    /* RB11, J2-5  -> HC-05 RXD */
const uint8_t PIN_BT_STATE = 17;    /* RB12, J2-6  <- HC-05 STATE */
const uint8_t PIN_BUZZER   = 9;     /* RB4,  J3-15 (optional) */
#define NSENS 5
uint8_t sensPin[NSENS] = { A0, A1, A2, A3, A4 };   /* left .. right */

const unsigned long USB_BAUD = 115200;
const unsigned long BT_BAUD  = 9600;
const unsigned long AT_BAUD  = 38400;

/* ---------------- tuning (change with commands) ---------------- */
int16_t kp100   = 90;       /* kp = 0.90 */
int16_t kd100   = 300;      /* kd = 3.00 */
int16_t baseSpd = 45;       /* % while following */
int16_t manSpd  = 60;       /* % in manual drive */
int16_t trimL   = 1500;     /* stop point, us */
int16_t trimR   = 1500;
int8_t  dirL    = +1;       /* +1: longer pulse = forward */
int8_t  dirR    = -1;
const int16_t SPAN_US = 500;            /* 100 % = trim +- 500 us */
bool     lineHigh = true;   /* true: the line gives the higher raw reading */
uint16_t deadMs   = 0;      /* manual deadman, 0 = off */
bool     telemetry = false;

/* ---------------- state ---------------- */
enum Mode { IDLE, MANUAL, FOLLOW, CALIB };
Mode mode = IDLE;

NanoServo servoL, servoR;   /* zeroed globals = detached (no constructor needed) */

int16_t calMin[NSENS] = { 0, 0, 0, 0, 0 };
int16_t calMax[NSENS] = { 1023, 1023, 1023, 1023, 1023 };
int16_t raw[NSENS], val[NSENS];
int16_t position = 2000, lastErr = 0;
bool    onLine = false;
unsigned long lostSince = 0, lastDriveCmd = 0, calStart = 0, lastTel = 0;
int16_t outL = 0, outR = 0;          /* last wheel commands, % */
bool    lastState = false;

/* one input line per port (declared up here, before the first function,
 * because the Arduino builder puts its auto-prototypes there) */
struct LineIn { char buf[32]; uint8_t len; unsigned long t; };
LineIn usbIn, btIn;

/* ---------------- output to both ports ---------------- */
void say(const char *s)
{
    Serial.print(s);   Serial.print("\r\n");
    Serial2.print(s);  Serial2.print("\r\n");
}

/* "1.50" from 150 */
void fix100(char *buf, int16_t v)
{
    int16_t a = v < 0 ? -v : v;
    sprintf(buf, "%s%d.%02d", v < 0 ? "-" : "", a / 100, a % 100);
}

/* "1.5" -> 150, "-0.25" -> -25, "3" -> 300 */
int16_t parse100(const char *s)
{
    while (*s == ' ') s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    v *= 100;
    if (*s == '.') {
        s++;
        if (*s >= '0' && *s <= '9') { v += (*s++ - '0') * 10;
            if (*s >= '0' && *s <= '9') v += (*s - '0'); }
    }
    if (v > 30000) v = 30000;
    return (int16_t)(neg ? -v : v);
}

void beep(unsigned int hz, unsigned long ms) { tone(PIN_BUZZER, hz, ms); }

/* ---------------- wheels ---------------- */
int16_t clampPct(int16_t p) { return p > 100 ? 100 : p < -100 ? -100 : p; }

void drive(int16_t l, int16_t r)
{
    outL = clampPct(l); outR = clampPct(r);
    if (!servoL.attached()) servoL.attach(PIN_SERVO_L, 1000 - 100, 2000 + 100);
    if (!servoR.attached()) servoR.attach(PIN_SERVO_R, 1000 - 100, 2000 + 100);
    servoL.writeMicroseconds(trimL + dirL * (int16_t)((long)outL * SPAN_US / 100));
    servoR.writeMicroseconds(trimR + dirR * (int16_t)((long)outR * SPAN_US / 100));
}

void motorsOff(void)            /* no pulses at all = standing still */
{
    outL = outR = 0;
    servoL.detach();
    servoR.detach();
}

void setMode(Mode m, const char *why)
{
    mode = m;
    if (m == IDLE) motorsOff();
    char b[48];
    static const char *names[] = { "IDLE", "MANUAL", "FOLLOW", "CALIBRATE" };
    snprintf(b, sizeof(b), "mode %s%s%s", names[m], why ? " - " : "", why ? why : "");
    say(b);
    beep(m == IDLE ? 880 : 1760, 60);
}

/* ---------------- sensors ---------------- */
void readSensors(void)
{
    long sum = 0, wsum = 0;
    onLine = false;
    for (uint8_t i = 0; i < NSENS; i++) {
        raw[i] = analogRead(sensPin[i]);
        long span = calMax[i] - calMin[i];
        if (span < 1) span = 1;
        long v = (long)(raw[i] - calMin[i]) * 1000L / span;
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        if (!lineHigh) v = 1000 - v;
        val[i] = (int16_t)v;
        if (v > 300) onLine = true;
        if (v > 50) { sum += v; wsum += v * i * 1000L; }   /* ignore floor noise */
    }
    if (onLine && sum > 0) {
        position = (int16_t)(wsum / sum);
    } else {
        /* lost: pretend it is fully to the side where it was last seen */
        position = position > 2000 ? (NSENS - 1) * 1000 : 0;
    }
}

/* ---------------- line following ---------------- */
void followStep(unsigned long now)
{
    readSensors();
    if (!onLine) {
        if (!lostSince) lostSince = now;
        if (now - lostSince > 1500) { setMode(IDLE, "line lost"); return; }
    } else {
        lostSince = 0;
    }
    int16_t e = (int16_t)((position - (NSENS - 1) * 500) / 20);   /* -100..100 */
    long turn = ((long)kp100 * e + (long)kd100 * (e - lastErr)) / 100L;
    lastErr = e;
    if (turn > 200) turn = 200;
    if (turn < -200) turn = -200;
    drive(baseSpd + (int16_t)turn, baseSpd - (int16_t)turn);
}

/* ---------------- calibration ---------------- */
void calibrateStart(void)
{
    /* the middle sensor is on the line now, the outer ones see the floor:
     * that tells us whether the line reads high or low */
    for (uint8_t i = 0; i < NSENS; i++) {
        raw[i] = analogRead(sensPin[i]);
        calMin[i] = 1023; calMax[i] = 0;
    }
    lineHigh = raw[NSENS / 2] > (raw[0] + raw[NSENS - 1]) / 2;
    calStart = millis();
    setMode(CALIB, lineHigh ? "line reads HIGH" : "line reads LOW");
}

void calibrateStep(unsigned long now)
{
    unsigned long t = now - calStart;
    for (uint8_t i = 0; i < NSENS; i++) {
        int16_t r = analogRead(sensPin[i]);
        if (r < calMin[i]) calMin[i] = r;
        if (r > calMax[i]) calMax[i] = r;
    }
    /* right 1 s, left 2 s, right 1 s: ends facing where it started */
    if      (t < 1000) drive( 35, -35);
    else if (t < 3000) drive(-35,  35);
    else if (t < 4000) drive( 35, -35);
    else {
        motorsOff();
        char b[64];
        bool ok = true;
        for (uint8_t i = 0; i < NSENS; i++) {
            snprintf(b, sizeof(b), "  S%d min %d max %d%s", i + 1, calMin[i], calMax[i],
                     calMax[i] - calMin[i] < 100 ? "  <- weak: check height / wiring" : "");
            say(b);
            if (calMax[i] - calMin[i] < 100) ok = false;
        }
        setMode(IDLE, ok ? "calibrated" : "calibration weak");
    }
}

/* ---------------- status ---------------- */
void printStatus(void)
{
    char b[80], p[8], d[8];
    fix100(p, kp100); fix100(d, kd100);
    static const char *names[] = { "IDLE", "MANUAL", "FOLLOW", "CALIBRATE" };
    snprintf(b, sizeof(b), "%s kp %s kd %s base %d%% man %d%%", names[mode], p, d, baseSpd, manSpd);
    say(b);
    snprintf(b, sizeof(b), "trim L %d R %d  dir L %d R %d  line %s  dead %u",
             trimL, trimR, dirL, dirR, lineHigh ? "HIGH" : "LOW", deadMs);
    say(b);
    snprintf(b, sizeof(b), "BT %s  servo SCCP L%u R%u", digitalRead(PIN_BT_STATE) ? "connected" : "no STATE",
             servoL.module(), servoR.module());
    say(b);
}

void printSensors(void)
{
    char b[80];
    readSensors();
    snprintf(b, sizeof(b), "raw %4d %4d %4d %4d %4d", raw[0], raw[1], raw[2], raw[3], raw[4]);
    say(b);
    snprintf(b, sizeof(b), "cal %4d %4d %4d %4d %4d  pos %d %s", val[0], val[1], val[2], val[3], val[4],
             position, onLine ? "" : "(no line)");
    say(b);
}

void printHelp(void)
{
    say("F B L R G I H J drive, S stop, 0-9 q speed, X/x follow on/off, C calibrate, V horn, ? status");
    say("kp n | kd n | base n | trim l|r us | pulse l|r us | dir l|r | inv | dead ms | tel | sensors");
}

/* ---------------- commands ---------------- */
void manual(int16_t l, int16_t r)
{
    if (mode == FOLLOW || mode == CALIB) setMode(MANUAL, 0);
    else mode = MANUAL;
    lastDriveCmd = millis();
    drive(l, r);
}

/* returns true if c was a single-character command */
bool singleChar(char c)
{
    int16_t s = manSpd, t = manSpd * 2 / 3, k = manSpd / 3;
    switch (c) {
    case 'F': manual( s,  s); return true;
    case 'B': manual(-s, -s); return true;
    case 'L': manual(-t,  t); return true;
    case 'R': manual( t, -t); return true;
    case 'G': manual( k,  s); return true;
    case 'I': manual( s,  k); return true;
    case 'H': manual(-k, -s); return true;
    case 'J': manual(-s, -k); return true;
    case 'S':
        if (mode == MANUAL) { motorsOff(); lastDriveCmd = millis(); }   /* RC apps send S often: stay quiet */
        return true;
    case 'D': if (mode != IDLE) setMode(IDLE, "stop"); return true;
    case 'q': manSpd = 100; return true;
    case 'X': case 'A':
        if (mode != FOLLOW) { lastErr = 0; lostSince = 0; setMode(FOLLOW, 0); }
        return true;
    case 'x': if (mode == FOLLOW) setMode(IDLE, 0); return true;
    case 'C': calibrateStart(); return true;
    case 'V': beep(1000, 300); return true;
    case 'v': case 'W': case 'w': case 'U': case 'u': return true;   /* RC app lights etc: ignored */
    case '?': printStatus(); return true;
    }
    if (c >= '0' && c <= '9') { manSpd = (c - '0') * 10; return true; }
    return false;
}

void lineCmd(char *s)
{
    char b[48], f[8];
    char *arg = strchr(s, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; } else arg = s + strlen(s);
    bool left = (arg[0] == 'l'), right = (arg[0] == 'r');
    char *num = strchr(arg, ' ');                 /* "trim l 1500" -> "1500" */
    num = num ? num + 1 : arg;

    if (!strcmp(s, "kp"))        { if (*arg) kp100 = parse100(arg); fix100(f, kp100); snprintf(b, sizeof(b), "kp %s", f); say(b); }
    else if (!strcmp(s, "kd"))   { if (*arg) kd100 = parse100(arg); fix100(f, kd100); snprintf(b, sizeof(b), "kd %s", f); say(b); }
    else if (!strcmp(s, "base")) { if (*arg) baseSpd = clampPct(atoi(arg)); snprintf(b, sizeof(b), "base %d%%", baseSpd); say(b); }
    else if (!strcmp(s, "trim") && (left || right)) {
        int v = atoi(num);
        if (v >= 1300 && v <= 1700) { if (left) trimL = v; else trimR = v; }
        snprintf(b, sizeof(b), "trim L %d R %d", trimL, trimR); say(b);
        if (mode == MANUAL && (outL || outR)) drive(outL, outR);
    }
    else if (!strcmp(s, "pulse") && (left || right)) {
        int v = atoi(num);
        if (v < 900 || v > 2100) { say("pulse 900..2100"); return; }
        if (mode != MANUAL) setMode(MANUAL, "raw pulse");
        lastDriveCmd = millis();
        drive(0, 0);                              /* both at trim, then override one */
        if (left) servoL.writeMicroseconds(v); else servoR.writeMicroseconds(v);
        snprintf(b, sizeof(b), "%s servo %d us (S = stop)", left ? "left" : "right", v); say(b);
    }
    else if (!strcmp(s, "dir") && (left || right)) {
        if (left) dirL = -dirL; else dirR = -dirR;
        snprintf(b, sizeof(b), "dir L %d R %d", dirL, dirR); say(b);
    }
    else if (!strcmp(s, "inv"))  { lineHigh = !lineHigh; say(lineHigh ? "line reads HIGH" : "line reads LOW"); }
    else if (!strcmp(s, "dead")) { deadMs = (uint16_t)atoi(arg); snprintf(b, sizeof(b), "dead %u ms", deadMs); say(b); }
    else if (!strcmp(s, "tel"))  { telemetry = !telemetry; say(telemetry ? "telemetry on (USB)" : "telemetry off"); }
    else if (!strcmp(s, "sensors")) printSensors();
    else if (!strcmp(s, "status"))  printStatus();
    else if (!strcmp(s, "help"))    printHelp();
    else { snprintf(b, sizeof(b), "? '%s' - type help", s); say(b); }
}

/* One input line per port. Upper case, digits and '?' act at once when they
 * are the first character. A lower-case letter starts a word command, ended
 * by Enter - except that RC apps send 'q', 'x', 'w', 'v', 'u' alone without
 * Enter: if one of those is followed by nothing for 150 ms it acts by itself. */
void feed(LineIn &in, int c, unsigned long now)
{
    if (c < 0) {                                       /* no new character: timeout? */
        if (in.len == 1 && now - in.t > 150 && strchr("qxvwu", in.buf[0])) {
            singleChar(in.buf[0]);
            in.len = 0;
        }
        return;
    }
    if (c == '\r' || c == '\n') {
        if (in.len) {
            in.buf[in.len] = 0;
            bool done = (in.len == 1) && singleChar(in.buf[0]);
            if (!done) lineCmd(in.buf);
        }
        in.len = 0;
        return;
    }
    if (in.len == 0 && !(c >= 'a' && c <= 'z')) {     /* immediate */
        if (c != ' ') singleChar((char)c);
        return;
    }
    if (in.len < sizeof(in.buf) - 1) in.buf[in.len++] = (char)c;
    in.t = now;
}

/* ---------------- HC-05 AT set-up bridge ---------------- */
void atBridge(void)
{
    Serial2.begin(AT_BAUD, PIN_BT_RX, PIN_BT_TX);
    Serial.println("HC-05 AT bridge at 38400. Power the HC-05 with its button held.");
    Serial.println("Serial Monitor: Both NL & CR. Try: AT   AT+NAME=dsPIC-Robot   AT+UART=9600,0,0");
    Serial.println("Press RESET to leave.");
    unsigned long t = 0;
    for (;;) {
        while (Serial.available())  Serial2.write((uint8_t)Serial.read());
        while (Serial2.available()) Serial.write((uint8_t)Serial2.read());
        if (millis() - t > 100) { t = millis(); digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); }
    }
}

/* ---------------- setup / loop ---------------- */
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(BUTTON_BUILTIN, INPUT);
    pinMode(PIN_BT_STATE, INPUT);
    CNPDBbits.CNPDB12 = 1;           /* pull-down: STATE not wired reads LOW */
    Serial.begin(USB_BAUD);
    delay(50);
    Serial.println();
    Serial.println("Lab 29 - line follower robot (servos + HC-05)");

    if (digitalRead(BUTTON_BUILTIN) == LOW) atBridge();     /* never returns */

    Serial2.begin(BT_BAUD, PIN_BT_RX, PIN_BT_TX);
    for (uint8_t i = 0; i < NSENS; i++) pinMode(sensPin[i], INPUT);
    motorsOff();
    say("Robot ready. C = calibrate, X = follow, F/B/L/R/S = drive, help = all commands");
    lastState = digitalRead(PIN_BT_STATE);
    beep(1320, 80);
}

void loop()
{
    static unsigned long tick = 0, ledT = 0, btnT = 0;
    static bool btnLast = true;
    unsigned long now = millis();

    /* commands from both ports */
    feed(usbIn, Serial.available()  ? Serial.read()  : -1, now);
    feed(btIn,  Serial2.available() ? Serial2.read() : -1, now);

    /* Bluetooth link dropped -> stop (only on a HIGH -> LOW edge, so an
     * unwired STATE pin, which always reads LOW, never stops anything) */
    bool st = digitalRead(PIN_BT_STATE);
    if (lastState && !st && mode != IDLE) setMode(IDLE, "Bluetooth disconnected");
    lastState = st;

    /* USER button: follow on / off */
    bool b = digitalRead(BUTTON_BUILTIN);
    if (b != btnLast && now - btnT > 30) {
        btnT = now; btnLast = b;
        if (!b) singleChar(mode == FOLLOW ? 'x' : 'X');
    }

    /* 100 Hz control step */
    if (now - tick >= 10) {
        tick = now;
        if (mode == FOLLOW) followStep(now);
        else if (mode == CALIB) calibrateStep(now);
        else if (mode == MANUAL && deadMs && (outL || outR) && now - lastDriveCmd > deadMs) {
            motorsOff();
            say("deadman stop");
        }
    }

    /* telemetry to USB only (Bluetooth at 9600 would slow the loop) */
    if (telemetry && now - lastTel >= 200) {
        lastTel = now;
        char t[64];
        if (mode != FOLLOW) readSensors();
        snprintf(t, sizeof(t), "pos %4d e %4d  L %4d R %4d  %s", position, lastErr, outL, outR, onLine ? "" : "no line");
        Serial.println(t);
    }

    /* LED: IDLE slow blink, MANUAL on, FOLLOW fast blink, CALIB very fast */
    unsigned long per = mode == IDLE ? 500 : mode == FOLLOW ? 100 : mode == CALIB ? 40 : 0;
    if (!per) digitalWrite(LED_BUILTIN, LED_ON);
    else if (now - ledT >= per) { ledT = now; digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); }
}
