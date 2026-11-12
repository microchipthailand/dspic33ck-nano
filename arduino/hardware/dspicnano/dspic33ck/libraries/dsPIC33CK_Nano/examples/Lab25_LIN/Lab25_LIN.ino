/*
 * Lab 25 - LIN bus (Local Interconnect Network): master + slave, 19200 baud
 * Board: dsPIC33CK Nano
 *
 * Goal:  LIN is the cheap single-wire car body bus (window lifters, mirrors,
 *        seat motors, ambient light). Build both ends:
 *          frame = BREAK (>= 13 bit low) + SYNC 0x55 + PID + 1..8 data + CHECKSUM
 *          PID   = 6-bit ID + 2 parity bits
 *          CHECKSUM: classic (data only, LIN 1.x) or enhanced (PID + data, LIN 2.x)
 *        The master sends every header; whoever "publishes" that ID answers.
 *
 * Roles - choose at RESET:  USER released = MASTER,  USER held = SLAVE
 *
 * Wiring:
 *   Test 1 - ONE board, no transceiver: jumper J2-5 (TX) -> J2-4 (RX).
 *            The master hears its own frames, exactly like on a real LIN wire,
 *            and checks every byte it sent (that is how LIN detects bit errors).
 *   Test 2 - real bus: per board a LIN transceiver (MCP2003B / TJA1021 / TLE7259):
 *     UART2 TX RB11 = D16 (J2-5)  -> transceiver TXD
 *     UART2 RX RB10 = D15 (J2-4)  <- transceiver RXD (open drain: needs pull-up,
 *                                     the sketch enables the internal one)
 *     transceiver CS/EN  -> 3V3     VBAT -> 12 V (bench supply), GND common
 *     LIN pins of all boards together; the MASTER needs 1 k + diode to VBAT.
 *
 * Frames used in this demo:
 *   ID 0x10  MASTER publishes 2 bytes: [LED command 0/1, counter]
 *   ID 0x20  SLAVE  publishes 4 bytes: [A0 hi, A0 lo, button, alive counter]
 *   Master schedule: 0x10, 0x20 alternately every 50 ms (a "schedule table").
 *   Press USER on the master: toggles the slave's LED through frame 0x10.
 *
 * Commands (master): c classic / e enhanced checksum   ? statistics
 *
 * ----------------------------------------------------------------------------
 * HOW IT WORKS
 *   UART2 in plain 8N1 mode at 19200 baud, driven register-level (like Lab 12).
 *   BREAK: U2MODE.BRKOVR = 1 forces TX low; hold it 13 bit times (677 us),
 *          release, then a 1-bit "break delimiter" - the receiver sees a 0x00
 *          byte with a framing error, which is how a slave detects the break.
 *   Every node reads back what it transmits (half duplex single wire): if the
 *   echoed byte differs, another node was driving -> bit error / collision.
 *   PID parity:  P0 = ID0 ^ ID1 ^ ID2 ^ ID4      P1 = !(ID1 ^ ID3 ^ ID4 ^ ID5)
 *   Checksum:    8-bit sum with carry wrap-around, then inverted.
 *   (The dsPIC33CK UART also has a hardware LIN mode, U2MODE.MOD = 1100, that
 *    automates break/sync/checksum. This lab builds the frame by hand so every
 *    field is visible; the hardware mode's exact sequencing is not verified.)
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <NanoSerial2.h>

const unsigned long LIN_BAUD = 19200;
const unsigned long BIT_US = 1000000UL / LIN_BAUD;     /* 52 us */

bool isMaster = true;
bool enhanced = true;                                   /* LIN 2.x checksum */
unsigned long okFrames = 0, noResponse = 0, badChecksum = 0, bitErrors = 0;

/* ---------------- LIN maths ---------------- */
uint8_t linPid(uint8_t id)
{
    id &= 0x3F;
    uint8_t b = id;
    uint8_t p0 = ((b >> 0) ^ (b >> 1) ^ (b >> 2) ^ (b >> 4)) & 1;
    uint8_t p1 = (~((b >> 1) ^ (b >> 3) ^ (b >> 4) ^ (b >> 5))) & 1;
    return (uint8_t)(id | (p0 << 6) | (p1 << 7));
}

uint8_t linChecksum(uint8_t pid, const uint8_t *d, uint8_t n, bool enh)
{
    uint16_t s = enh ? pid : 0;
    for (uint8_t i = 0; i < n; i++) { s += d[i]; if (s > 0xFF) s -= 0xFF; }   /* add with carry */
    return (uint8_t)~s;
}

/* ---------------- physical layer ---------------- */
void flushRx(void) { while (Serial2.available()) Serial2.read(); }

/* send a byte and read it back from the bus (LIN is half duplex) */
bool txEcho(uint8_t b)
{
    Serial2.write(b);
    unsigned long t0 = micros();
    while (Serial2.available() == 0) if (micros() - t0 > 20 * BIT_US) { bitErrors++; return false; }
    int e = Serial2.read();
    if (e != b) { bitErrors++; return false; }
    return true;
}

void sendBreak(void)
{
    Serial2.flush();                                   /* previous byte fully out */
    U2MODEbits.BRKOVR = 1;                             /* force TX low ... */
    delayMicroseconds(13 * BIT_US);                    /* ... 13 bit times */
    U2MODEbits.BRKOVR = 0;
    delayMicroseconds(BIT_US);                         /* break delimiter (1 bit high) */
    delayMicroseconds(2 * BIT_US);
    flushRx();                                         /* drop the 0x00/framing-error byte the break produced */
    U2STAbits.FERR = 0;
}

/* receive n bytes with an inter-byte timeout; returns count */
int rxBytes(uint8_t *b, int n, unsigned long timeoutUs)
{
    int k = 0; unsigned long t0 = micros();
    while (k < n) {
        if (Serial2.available()) { b[k++] = (uint8_t)Serial2.read(); t0 = micros(); }
        else if (micros() - t0 > timeoutUs) break;
    }
    return k;
}

/* ---------------- master ---------------- */
void masterHeader(uint8_t id)
{
    sendBreak();
    txEcho(0x55);                                      /* SYNC: 01010101 - slaves measure the baud from it */
    txEcho(linPid(id));
}

void masterPublish(uint8_t id, const uint8_t *d, uint8_t n)
{
    masterHeader(id);
    for (uint8_t i = 0; i < n; i++) txEcho(d[i]);
    txEcho(linChecksum(linPid(id), d, n, enhanced));
}

bool masterRequest(uint8_t id, uint8_t *d, uint8_t n)
{
    flushRx();
    masterHeader(id);
    uint8_t buf[9];
    int got = rxBytes(buf, n + 1, 1400 * BIT_US / 100);   /* response window ~ 1.4 x nominal */
    if (got < n + 1) { noResponse++; return false; }
    if (buf[n] != linChecksum(linPid(id), buf, n, enhanced)) { badChecksum++; return false; }
    for (uint8_t i = 0; i < n; i++) d[i] = buf[i];
    okFrames++;
    return true;
}

/* ---------------- slave ---------------- */
uint8_t slaveLed = 0, slaveAlive = 0;

void slavePoll(void)
{
    /* wait for a break: the UART reports it as a 0x00 byte with FERR set */
    if (!Serial2.available()) return;
    int c = Serial2.read();
    if (c != 0x00) return;                              /* not a break - ignore */
    uint8_t h[2];
    if (rxBytes(h, 2, 20 * BIT_US) < 2 || h[0] != 0x55) return;
    uint8_t pid = h[1], id = pid & 0x3F;
    if (pid != linPid(id)) { badChecksum++; return; }   /* parity error */

    if (id == 0x10) {                                   /* master publishes: receive */
        uint8_t d[3];
        if (rxBytes(d, 3, 20 * BIT_US) == 3 && d[2] == linChecksum(pid, d, 2, enhanced)) {
            slaveLed = d[0];
            digitalWrite(LED_BUILTIN, slaveLed ? LED_ON : LED_OFF);
            okFrames++;
        } else badChecksum++;
    } else if (id == 0x20) {                            /* we publish: respond */
        int a = analogRead(A0);
        uint8_t d[4] = { (uint8_t)(a >> 8), (uint8_t)a, (uint8_t)(digitalRead(BUTTON_BUILTIN) == LOW), slaveAlive++ };
        delayMicroseconds(2 * BIT_US);                  /* response space */
        for (int i = 0; i < 4; i++) txEcho(d[i]);
        txEcho(linChecksum(pid, d, 4, enhanced));
        okFrames++;
    }
}

/* ---------------- setup / loop ---------------- */
void stats(void)
{
    Serial.print("# ok="); Serial.print(okFrames);
    Serial.print(" noResponse="); Serial.print(noResponse);
    Serial.print(" badChecksum="); Serial.print(badChecksum);
    Serial.print(" bitErrors="); Serial.print(bitErrors);
    Serial.println(enhanced ? "  checksum=ENHANCED (LIN 2.x)" : "  checksum=CLASSIC (LIN 1.x)");
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    isMaster = digitalRead(BUTTON_BUILTIN) == HIGH;
    Serial2.begin(LIN_BAUD);                            /* RX RB10 (J2-4), TX RB11 (J2-5) */
    pinMode(SERIAL2_DEFAULT_RX, INPUT_PULLUP);          /* LIN transceiver RXD is open drain */
    Serial.print("Lab 25 - LIN 19200 baud, ");
    Serial.println(isMaster ? "MASTER (schedule 0x10 / 0x20)" : "SLAVE (answers ID 0x20, listens to 0x10)");
    Serial.print("# PID(0x10)=0x"); Serial.print(linPid(0x10), HEX);
    Serial.print("  PID(0x20)=0x"); Serial.println(linPid(0x20), HEX);
    if (!isMaster) while (digitalRead(BUTTON_BUILTIN) == LOW) { }
}

void loop()
{
    if (!isMaster) { slavePoll(); return; }

    static unsigned long last = 0;
    static bool slot = false;
    static uint8_t ledCmd = 0, counter = 0;
    static int lastBtn = HIGH;

    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) { ledCmd ^= 1; Serial.print("# LED command -> "); Serial.println(ledCmd); delay(30); }
    lastBtn = btn;

    if (millis() - last >= 50) {                        /* schedule table: one slot every 50 ms */
        last = millis();
        slot = !slot;
        if (slot) {
            uint8_t d[2] = { ledCmd, counter++ };
            masterPublish(0x10, d, 2);
        } else {
            uint8_t d[4];
            static unsigned long lastPrint = 0;
            if (masterRequest(0x20, d, 4)) {
                if (millis() - lastPrint > 500) {
                    lastPrint = millis();
                    Serial.print("slave 0x20: A0="); Serial.print((d[0] << 8) | d[1]);
                    Serial.print(" btn="); Serial.print(d[2]); Serial.print(" alive="); Serial.println(d[3]);
                }
            } else if (millis() - lastPrint > 2000) {
                lastPrint = millis();
                Serial.println("# no answer to 0x20 (single board + jumper: expected - no slave). Frames are still echo-checked:");
                stats();
            }
        }
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'c') { enhanced = false; stats(); }
        else if (c == 'e') { enhanced = true; stats(); }
        else if (c == '?') stats();
    }
}
