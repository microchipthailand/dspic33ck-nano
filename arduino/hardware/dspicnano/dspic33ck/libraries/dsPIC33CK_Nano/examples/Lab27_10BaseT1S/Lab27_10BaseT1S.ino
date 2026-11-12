/*
 * Lab 27 - 10BASE-T1S single-pair Ethernet with a LAN8651 MAC-PHY over SPI
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS recommended
 *
 * Goal:  put the board on automotive/industrial single-pair Ethernet
 *        (IEEE 802.3cg 10BASE-T1S: 10 Mbit/s, one twisted pair, MULTIDROP - up
 *        to 8+ nodes on the same pair, collision-free thanks to PLCA).
 *        The dsPIC33CK has no Ethernet MAC, so the MAC *and* PHY live in a
 *        Microchip LAN8650/LAN8651 "MAC-PHY", talking OPEN Alliance TC6
 *        ("10BASE-T1x MAC-PHY Serial Interface") over SPI.
 *
 * Parts: LAN8651 board, e.g. MikroE "Two-Wire ETH Click" (LAN8651, 3.3 V),
 *        two of them (two Nano boards), or one + a 10BASE-T1S USB adapter
 *        (e.g. Microchip EVB-LAN8670-USB) on a PC with Wireshark.
 *
 * Wiring (Click pin names):
 *   SCK  -> RC4 (J3-6)     MOSI(SDI) -> RC5 (J3-5)     MISO(SDO) <- RC8 (J3-14)
 *   CS   -> RC9 (J3-13)    IRQ (active low) -> RB7 (J3-9)     RST -> RD8 (J3-12)
 *   3V3, GND.  T1S pair: N<->N, P<->P between the boards, terminations per the
 *   click's jumpers (100 ohm at both ends of the pair).
 *
 * What it does:
 *   1. hardware reset, wait for RESETC, read OA_ID and OA_PHYID over TC6
 *   2. configure PLCA: this node's ID (0 = coordinator, others 1..7), node count
 *   3. enable the MAC (TX/RX), then every second send an Ethernet frame:
 *        dst ff:ff:ff:ff:ff:ff, src 02:00:00:00:00:<node>, EtherType 0x88B5
 *        payload "dsPIC33CK Nano node N count C"
 *      and print every frame received (with source MAC + text)
 *   Choose the node ID at RESET: USER released = node 0 (coordinator),
 *   USER held = node 1.
 *
 * ----------------------------------------------------------------------------
 * OPEN ALLIANCE TC6 IN 30 SECONDS
 *   Every SPI transfer starts with a 32-bit header (MSB first, last bit = odd parity).
 *   CONTROL transaction (register access):
 *     header: DNC=0 | HDRB=0 | WNR(1=write) | AID=0 | MMS(4) | ADDR(16) | LEN(7) | P
 *     a write sends header + data words; the MAC-PHY echoes the header one word
 *     later, so the transfer is 1 word longer than you'd expect.
 *     MMS = memory map selector: 0 = standard/OA registers, 1 = MAC,
 *           4 = PHY vendor specific (PLCA lives in MMS 4 on LAN865x)
 *   DATA transaction (Ethernet frames), in 64-byte chunks:
 *     host -> MAC-PHY:  4-byte header (DNC=1, DV, SV, SWO, EV, EBO, ...) + 64 bytes
 *     MAC-PHY -> host:  64 bytes + 4-byte footer (EXST, HDRB, SYNC, RCA, DV, SV,
 *                       SWO, EV, EBO, TXC, P)  - RCA = receive chunks available,
 *                       TXC = transmit credits (free chunk buffers)
 *   A frame of up to 1518 bytes is cut into 64-byte chunks: SV (start valid)
 *   on the first, EV (end valid) + EBO (end byte offset) on the last.
 *
 * NOT VERIFIED on hardware: this lab is written from the OPEN Alliance TC6
 * specification and the LAN865x register map as I know them. Register
 * addresses and the LAN8651's recommended init writes (Microchip application
 * note / driver "lan865x") should be checked before relying on it; the code
 * prints every step so you can see where it stops.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

const uint8_t PIN_CS  = 30;   /* RC9 = SS */
const uint8_t PIN_IRQ = 12;   /* RB7 */
const uint8_t PIN_RST = 36;   /* RD8 */

uint8_t nodeId = 0;
const uint8_t NODE_COUNT = 8;

/* ---- TC6 / LAN865x register addresses (MMS, address) ---- */
#define MMS_OA   0
#define MMS_MAC  1
#define MMS_VS   4
#define OA_ID       0x0000
#define OA_PHYID    0x0001
#define OA_STDCAP   0x0002
#define OA_RESET    0x0003
#define OA_CONFIG0  0x0004
#define OA_STATUS0  0x0008
#define OA_BUFSTS   0x000B
#define MAC_NCR     0x0000      /* network control: TXEN bit 3, RXEN bit 2 */
#define MAC_NCFGR   0x0001      /* network config: bit 0... copy all frames etc. */
#define MAC_SAB1    0x0022      /* specific address 1 bottom (MAC bytes 0..3) */
#define MAC_SAT1    0x0023      /* specific address 1 top    (MAC bytes 4..5) */
#define VS_PLCA_CTRL0  0xCA01   /* EN bit 15 */
#define VS_PLCA_CTRL1  0xCA02   /* NCNT[15:8] | ID[7:0] */

SPISettings_t tc6;            /* set in macPhyInit(): 10 MHz, MSB first, mode 0 (LAN8651: up to 25 MHz) */

/* ---------------- TC6 helpers ---------------- */
static uint32_t withParity(uint32_t h)   /* set bit 0 so that the 32-bit word has ODD parity */
{
    h &= ~1UL;
    uint32_t x = h; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return h | ((~x) & 1UL);
}

static uint32_t xfer32(uint32_t w)
{
    uint32_t r = 0;
    for (int i = 3; i >= 0; i--) r = (r << 8) | SPI.transfer((uint8_t)(w >> (8 * i)));
    return r;
}

static uint32_t ctrlHeader(bool write, uint8_t mms, uint16_t addr, uint8_t lenMinus1)
{
    uint32_t h = 0;
    h |= (uint32_t)(write ? 1 : 0) << 29;            /* WNR */
    h |= (uint32_t)(mms & 0x0F) << 24;               /* MMS */
    h |= (uint32_t)addr << 8;                        /* ADDR */
    h |= (uint32_t)(lenMinus1 & 0x7F) << 1;          /* LEN */
    return withParity(h);                            /* DNC = 0: control */
}

uint32_t regRead(uint8_t mms, uint16_t addr)
{
    SPI.beginTransaction(tc6);
    digitalWrite(PIN_CS, LOW);
    xfer32(ctrlHeader(false, mms, addr, 0));
    xfer32(0);                                       /* echoed header comes back here */
    uint32_t v = xfer32(0);                          /* register value */
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();
    return v;
}

void regWrite(uint8_t mms, uint16_t addr, uint32_t val)
{
    SPI.beginTransaction(tc6);
    digitalWrite(PIN_CS, LOW);
    xfer32(ctrlHeader(true, mms, addr, 0));
    xfer32(val);
    xfer32(0);                                       /* clock out the echo */
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();
}

/* ---------------- data chunks ---------------- */
void handleFrame(const uint8_t *f, uint16_t len);
#define CHUNK 64
uint8_t rxFrame[1536]; uint16_t rxLen = 0;
unsigned long txFrames = 0, rxFrames = 0, chunkErrors = 0;
uint8_t txCredits = 0, rxAvail = 0;

/* one data chunk both ways. tx may be NULL (then DV = 0, we only receive). */
void dataChunk(const uint8_t *tx, bool sv, bool ev, uint8_t ebo)
{
    uint32_t h = 1UL << 31;                          /* DNC = 1: data */
    if (tx) {
        h |= 1UL << 21;                              /* DV: this chunk carries TX data */
        if (sv) h |= 1UL << 20;                      /* SV: start of frame, SWO = 0 */
        if (ev) { h |= 1UL << 14; h |= (uint32_t)(ebo & 0x3F) << 8; }   /* EV + EBO */
    }
    h = withParity(h);

    uint8_t rx[CHUNK];
    SPI.beginTransaction(tc6);
    digitalWrite(PIN_CS, LOW);
    xfer32(h);
    for (int i = 0; i < CHUNK; i++) rx[i] = SPI.transfer(tx ? tx[i] : 0);
    uint32_t f = xfer32(0);                          /* footer */
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();

    /* footer: EXST b31 | HDRB b30 | SYNC b29 | RCA b28..24 | DV b21 | SV b20 |
     *         SWO b19..16 | EV b14 | EBO b13..8 | TXC b5..1 | P b0 */
    if (f & (1UL << 30)) { chunkErrors++; return; }  /* header bad (parity) */
    rxAvail   = (uint8_t)((f >> 24) & 0x1F);
    txCredits = (uint8_t)((f >> 1) & 0x1F);
    if (!(f & (1UL << 21))) return;                  /* no RX data in this chunk */
    bool rsv = f & (1UL << 20), rev = f & (1UL << 14);
    uint8_t swo = (uint8_t)((f >> 16) & 0x0F) * 4, rebo = (uint8_t)((f >> 8) & 0x3F);
    int from = rsv ? swo : 0, to = rev ? rebo + 1 : CHUNK;
    if (rsv) rxLen = 0;
    for (int i = from; i < to && rxLen < sizeof(rxFrame); i++) rxFrame[rxLen++] = rx[i];
    if (rev) { rxFrames++; handleFrame(rxFrame, rxLen); rxLen = 0; }
}

void sendFrame(const uint8_t *frame, uint16_t len)
{
    if (len < 60) len = 60;                          /* Ethernet minimum (FCS added by the MAC) */
    uint8_t buf[CHUNK];
    for (uint16_t off = 0; off < len; off += CHUNK) {
        uint16_t n = (uint16_t)min((uint16_t)CHUNK, (uint16_t)(len - off));
        memset(buf, 0, CHUNK);
        memcpy(buf, frame + off, n);
        bool last = off + CHUNK >= len;
        dataChunk(buf, off == 0, last, (uint8_t)(n - 1));
    }
    txFrames++;
}

/* ---------------- application ---------------- */
uint8_t myMac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };

void handleFrame(const uint8_t *f, uint16_t len)
{
    if (len < 14) return;
    uint16_t type = (uint16_t)((f[12] << 8) | f[13]);
    Serial.print("rx "); Serial.print(len); Serial.print(" B from ");
    for (int i = 6; i < 12; i++) { if (f[i] < 16) Serial.print('0'); Serial.print(f[i], HEX); if (i < 11) Serial.print(':'); }
    Serial.print(" type 0x"); Serial.print(type, HEX);
    if (type == 0x88B5) {
        Serial.print("  \"");
        for (uint16_t i = 14; i < len && f[i]; i++) Serial.write(f[i]);
        Serial.print('"');
        digitalWrite(LED_BUILTIN, LED_ON);
    }
    Serial.println();
}

bool macPhyInit(void)
{
    pinMode(PIN_CS, OUTPUT); digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_IRQ, INPUT_PULLUP);
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW); delay(5); digitalWrite(PIN_RST, HIGH);  /* hardware reset */
    delay(10);
    SPI.begin();
    tc6 = SPISettings(10000000UL, MSBFIRST, SPI_MODE0);

    uint32_t id = regRead(MMS_OA, OA_ID), phy = regRead(MMS_OA, OA_PHYID);
    Serial.print("# OA_ID=0x"); Serial.print(id, HEX);
    Serial.print("  OA_PHYID=0x"); Serial.println(phy, HEX);
    if (phy == 0 || phy == 0xFFFFFFFFUL) { Serial.println("# no MAC-PHY answering - check SPI wiring / RST / power"); return false; }

    uint32_t st = regRead(MMS_OA, OA_STATUS0);
    Serial.print("# STATUS0=0x"); Serial.print(st, HEX); Serial.println(st & (1UL << 6) ? " (RESETC set: reset complete)" : "");
    regWrite(MMS_OA, OA_STATUS0, st);                /* write-1-to-clear the status bits */

    /* MAC address = 02:00:00:00:00:<node> (locally administered) */
    myMac[5] = nodeId;
    regWrite(MMS_MAC, MAC_SAB1, (uint32_t)myMac[0] | ((uint32_t)myMac[1] << 8) | ((uint32_t)myMac[2] << 16) | ((uint32_t)myMac[3] << 24));
    regWrite(MMS_MAC, MAC_SAT1, (uint32_t)myMac[4] | ((uint32_t)myMac[5] << 8));

    /* PLCA: node 0 is the coordinator and sends the BEACON; node count = 8 */
    regWrite(MMS_VS, VS_PLCA_CTRL1, ((uint32_t)NODE_COUNT << 8) | nodeId);
    regWrite(MMS_VS, VS_PLCA_CTRL0, 1UL << 15);      /* PLCA enable */

    /* TC6 configuration: SYNC = 1 tells the MAC-PHY the host is configured */
    uint32_t c0 = regRead(MMS_OA, OA_CONFIG0);
    regWrite(MMS_OA, OA_CONFIG0, c0 | (1UL << 15));

    /* MAC: receive broadcast, enable TX + RX */
    regWrite(MMS_MAC, MAC_NCR, (1UL << 3) | (1UL << 2));
    Serial.print("# PLCA node "); Serial.print(nodeId); Serial.print(" of "); Serial.print(NODE_COUNT);
    Serial.println(nodeId == 0 ? " (coordinator)" : "");
    return true;
}

bool ready = false;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    nodeId = (digitalRead(BUTTON_BUILTIN) == LOW) ? 1 : 0;
    Serial.println("Lab 27 - 10BASE-T1S via LAN8651 (OPEN Alliance TC6 over SPI)");
    ready = macPhyInit();
}

void loop()
{
    if (!ready) { digitalWrite(LED_BUILTIN, (millis() / 100) % 2 ? LED_ON : LED_OFF); return; }

    /* poll for received chunks: IRQ goes low when RCA > 0 (or on errors) */
    if (digitalRead(PIN_IRQ) == LOW || rxAvail > 0) dataChunk(NULL, false, false, 0);

    static unsigned long last = 0, count = 0;
    if (millis() - last >= 1000) {
        last = millis();
        uint8_t f[64] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        memcpy(f + 6, myMac, 6);
        f[12] = 0x88; f[13] = 0xB5;                  /* local experimental EtherType */
        char msg[40];
        snprintf(msg, sizeof(msg), "dsPIC33CK Nano node %u count %lu", nodeId, count++);
        memcpy(f + 14, msg, strlen(msg) + 1);
        sendFrame(f, 64);
        digitalWrite(LED_BUILTIN, LED_OFF);
        Serial.print("# tx "); Serial.print(txFrames); Serial.print("  rx "); Serial.print(rxFrames);
        Serial.print("  credits "); Serial.print(txCredits); Serial.print("  chunk errors "); Serial.println(chunkErrors);
    }
}
