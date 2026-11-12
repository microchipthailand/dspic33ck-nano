/*
 * Lab 24 - Modbus RTU over RS485: Master and Slave in one sketch
 * Board: dsPIC33CK Nano (use TWO boards, or one board + a PC Modbus tool)
 *
 * Goal:  build a real industrial fieldbus: RS485 half-duplex physical layer,
 *        Modbus RTU frames, CRC-16, timeouts, function codes 03/06.
 *
 * Choose the role at power-up / RESET:
 *   USER released  -> SLAVE  (address 1)   LED blinks slowly
 *   USER held      -> MASTER                LED flashes on every reply
 *
 * Parts (per board): MAX485 / SP3485 / THVD1500 RS485 transceiver module
 *   (3.3 V part preferred - SP3485/THVD1500; a 5 V MAX485 module needs its RO
 *   output divided down to 3.3 V).
 *
 * Wiring (each board):
 *   RS485 RO  -> RB10 = D15 (J2-4)   Serial2 RX
 *   RS485 DI  <- RB11 = D16 (J2-5)   Serial2 TX
 *   RS485 DE + /RE (tied together) <- RB12 = D17 (J2-6)  HIGH = transmit
 *   RS485 VCC -> 3V3, GND -> GND
 *   A <-> A, B <-> B between the boards (twisted pair), 120 ohm across A-B at
 *   both ends of a long cable. Join the GNDs too.
 *
 * Slave register map (holding registers, function 03 read / 06 write):
 *   40001  reg 0  analogRead(A0)        (read only, pot on RA0 J2-15)
 *   40002  reg 1  USER button 1/0        (read only)
 *   40003  reg 2  LED: 0 off, 1 on       (read / WRITE)
 *   40004  reg 3  uptime seconds         (read only)
 *   40005  reg 4  request counter        (read only)
 *
 * Master: every 500 ms reads regs 0..4 from slave 1 and prints them.
 *         USER press -> writes reg 2 (toggles the slave's LED).
 * PC test: Modbus Poll / QModMaster / mbpoll on a USB-RS485 dongle,
 *          9600 8N1, slave id 1, holding registers 0..4.
 */

#include <Arduino.h>
#include <NanoSerial2.h>

const unsigned long MB_BAUD  = 9600;
const uint8_t       SLAVE_ID = 1;
const uint8_t       PIN_DE   = SERIAL2_DEFAULT_DE;   /* RB12 */
const uint16_t      NREGS    = 5;

/* Modbus RTU: a frame ends after 3.5 character times of silence.
 * 1 char = 11 bits (start + 8 + parity/stop + stop) -> 3.5 * 11 / 9600 = 4.0 ms.
 * Above 19200 baud the spec fixes it at 1.75 ms. */
const unsigned long T35_US = (MB_BAUD > 19200UL) ? 1750UL : (38500000UL / MB_BAUD);

bool isMaster = false;
uint16_t regs[NREGS];
unsigned long requests = 0;

/* ---------- CRC-16/MODBUS: poly 0xA001 (reflected 0x8005), init 0xFFFF ---------- */
uint16_t crc16(const uint8_t *b, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *b++;
        for (int i = 0; i < 8; i++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

/* ---------- frame I/O ---------- */
void sendFrame(uint8_t *f, uint16_t n)
{
    uint16_t c = crc16(f, n);
    f[n++] = (uint8_t)(c & 0xFF);       /* CRC low byte first */
    f[n++] = (uint8_t)(c >> 8);
    Serial2.write(f, n);                /* DE goes HIGH on the first byte */
    Serial2.flush();                    /* waits for the last stop bit, then DE LOW */
}

/* Wait up to waitMs for the first byte (0 = don't wait, return at once if the
 * line is quiet), then collect bytes until T3.5 of silence ends the frame.
 * Returns the frame length, 0 if nothing arrived. */
uint16_t receiveFrame(uint8_t *f, uint16_t maxLen, unsigned long waitMs)
{
    unsigned long t0 = millis();
    while (Serial2.available() == 0) {
        if (millis() - t0 >= waitMs) return 0;
    }
    uint16_t n = 0;
    unsigned long last = micros();
    while (micros() - last < T35_US) {
        if (Serial2.available() > 0) {
            int c = Serial2.read();
            if (n < maxLen) f[n++] = (uint8_t)c;
            last = micros();
        }
    }
    return n;
}

bool crcOk(const uint8_t *f, uint16_t n)
{
    if (n < 4) return false;
    uint16_t c = crc16(f, (uint16_t)(n - 2));
    return f[n - 2] == (uint8_t)(c & 0xFF) && f[n - 1] == (uint8_t)(c >> 8);
}

/* ======================= SLAVE ======================= */
void slaveException(uint8_t fn, uint8_t code)
{
    uint8_t r[5] = { SLAVE_ID, (uint8_t)(fn | 0x80), code };
    sendFrame(r, 3);
}

void slavePoll(void)
{
    regs[0] = (uint16_t)analogRead(A0);
    regs[1] = digitalRead(BUTTON_BUILTIN) == LOW ? 1 : 0;
    regs[3] = (uint16_t)(millis() / 1000UL);
    regs[4] = (uint16_t)requests;

    uint8_t f[64];
    uint16_t n = receiveFrame(f, sizeof(f), 0);   /* non-blocking: loop() keeps running */
    if (n == 0) return;
    if (!crcOk(f, n) || f[0] != SLAVE_ID) return;   /* not for us / corrupt: stay silent */
    if (n != 8) { slaveException(f[1], 0x03); return; }   /* fn 03 and 06 requests are 8 bytes */
    requests++;

    uint8_t  fn   = f[1];
    uint16_t addr = (uint16_t)((f[2] << 8) | f[3]);
    uint16_t val  = (uint16_t)((f[4] << 8) | f[5]);

    if (fn == 0x03) {                                /* Read Holding Registers */
        uint16_t qty = val;
        if (qty < 1 || qty > 125) { slaveException(fn, 0x03); return; }
        if ((uint32_t)addr + qty > NREGS) { slaveException(fn, 0x02); return; }
        uint8_t r[3 + 2 * NREGS + 2];
        r[0] = SLAVE_ID; r[1] = 0x03; r[2] = (uint8_t)(qty * 2);
        for (uint16_t i = 0; i < qty; i++) {
            r[3 + 2 * i] = (uint8_t)(regs[addr + i] >> 8);
            r[4 + 2 * i] = (uint8_t)(regs[addr + i] & 0xFF);
        }
        sendFrame(r, (uint16_t)(3 + 2 * qty));
    } else if (fn == 0x06) {                         /* Write Single Register */
        if (addr != 2) { slaveException(fn, 0x02); return; }   /* only the LED is writable */
        regs[2] = val ? 1 : 0;
        digitalWrite(LED_BUILTIN, regs[2] ? LED_ON : LED_OFF);
        sendFrame(f, 6);                             /* echo request = success */
    } else {
        slaveException(fn, 0x01);                    /* illegal function */
    }
}

/* ======================= MASTER ======================= */
/* returns 0 ok, 1 timeout, 2 CRC, 3 exception (code in *exc) */
int masterReadRegs(uint8_t id, uint16_t addr, uint16_t qty, uint16_t *out, uint8_t *exc)
{
    uint8_t f[8] = { id, 0x03, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(qty >> 8), (uint8_t)qty };
    while (Serial2.available()) Serial2.read();     /* flush stale bytes */
    sendFrame(f, 6);
    uint8_t r[64];
    uint16_t n = receiveFrame(r, sizeof(r), 200);
    if (n == 0) return 1;
    if (!crcOk(r, n)) return 2;
    if (r[1] & 0x80) { *exc = r[2]; return 3; }
    for (uint16_t i = 0; i < qty; i++) out[i] = (uint16_t)((r[3 + 2 * i] << 8) | r[4 + 2 * i]);
    return 0;
}

int masterWriteReg(uint8_t id, uint16_t addr, uint16_t value)
{
    uint8_t f[8] = { id, 0x06, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(value >> 8), (uint8_t)value };
    while (Serial2.available()) Serial2.read();
    sendFrame(f, 6);
    uint8_t r[16];
    uint16_t n = receiveFrame(r, sizeof(r), 200);
    if (n == 0) return 1;
    if (!crcOk(r, n)) return 2;
    return (r[1] & 0x80) ? 3 : 0;
}

void masterPoll(void)
{
    static unsigned long last = 0;
    static uint16_t ledCmd = 0;
    static int lastBtn = HIGH;
    static unsigned long ok = 0, fail = 0;

    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        ledCmd ^= 1;
        int e = masterWriteReg(SLAVE_ID, 2, ledCmd);
        Serial.print("write LED=");
        Serial.print(ledCmd);
        Serial.println(e == 0 ? " ok" : " FAILED");
        delay(30);
    }
    lastBtn = btn;

    if (millis() - last < 500) return;
    last = millis();

    uint16_t v[NREGS];
    uint8_t exc = 0;
    int e = masterReadRegs(SLAVE_ID, 0, NREGS, v, &exc);
    if (e == 0) {
        ok++;
        digitalWrite(LED_BUILTIN, LED_ON);
        Serial.print("slave1: A0="); Serial.print(v[0]);
        Serial.print(" btn=");       Serial.print(v[1]);
        Serial.print(" led=");       Serial.print(v[2]);
        Serial.print(" up=");        Serial.print(v[3]);
        Serial.print("s req=");      Serial.print(v[4]);
        Serial.print("  (ok ");      Serial.print(ok);
        Serial.print(" / fail ");    Serial.print(fail);
        Serial.println(")");
        delay(20);
        digitalWrite(LED_BUILTIN, LED_OFF);
    } else {
        fail++;
        Serial.print("read failed: ");
        Serial.println(e == 1 ? "timeout (check A/B, DE wiring, slave powered?)" :
                       e == 2 ? "CRC error (noise? termination? baud?)" : "exception from slave");
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);

    isMaster = (digitalRead(BUTTON_BUILTIN) == LOW);
    Serial2.begin(MB_BAUD, SERIAL2_DEFAULT_RX, SERIAL2_DEFAULT_TX, PIN_DE);

    Serial.print("Lab 24 - Modbus RTU ");
    Serial.println(isMaster ? "MASTER (polling slave 1)" : "SLAVE id 1 (registers 0..4)");
    if (isMaster) {
        while (digitalRead(BUTTON_BUILTIN) == LOW) { }   /* wait for release */
        delay(50);
    }
}

void loop()
{
    if (isMaster) {
        masterPoll();
    } else {
        slavePoll();
        /* LED = register 2 (written by the master). Until the master has
         * written it once, blink slowly to show "slave alive, waiting". */
        static bool everWritten = false;
        if (regs[2]) everWritten = true;
        if (everWritten) digitalWrite(LED_BUILTIN, regs[2] ? LED_ON : LED_OFF);
        else             digitalWrite(LED_BUILTIN, (millis() / 1000) % 2 ? LED_ON : LED_OFF);
    }
}
