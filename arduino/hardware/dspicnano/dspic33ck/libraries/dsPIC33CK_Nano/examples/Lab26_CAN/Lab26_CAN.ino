/*
 * Lab 26 - CAN FD controller: send / receive frames, filters, loopback, a real bus
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS (required - CAN clock from the PLL)
 *
 * Goal:  use the built-in CAN FD controller (CAN 2.0B classic frames in this
 *        lab): bit timing, the transmit queue, a receive FIFO with an
 *        acceptance filter, error counters - first in INTERNAL LOOPBACK on a
 *        single board, then on a real bus with a transceiver.
 *
 * Step 1 - no parts: start in LOOPBACK. The controller receives its own frames.
 * Step 2 - real bus: 2 boards (or 1 board + a USB-CAN adapter / PCAN / CANable).
 *   Transceiver: MCP2562(FD) or SN65HVD230/TJA1051 module, 3.3 V logic I/O.
 *   CAN TX = RC6 = D27 (J2-22)  -> transceiver TXD
 *   CAN RX = RC7 = D28 (J3-18)  <- transceiver RXD
 *   transceiver VCC 5V (J2-1) [MCP2562: VDD=5V, VIO=3V3 (J3-1)], GND, STBY -> GND
 *   CANH <-> CANH, CANL <-> CANL, 120 ohm at BOTH ends of the bus.
 *   Send 'n' to switch to NORMAL mode (needs at least one other node to ACK).
 *
 * Bus: 500 kbit/s, 11-bit IDs. This node sends ID 0x123 every 500 ms:
 *   data = [counter, A0 high, A0 low, button, 0xCA, 0xFE, 0, 0]
 *   It receives IDs 0x100..0x1FF (filter 0 + mask) into FIFO1 and prints them.
 *   ID 0x120 byte0 = 0/1 switches this board's LED (remote control).
 *
 * Commands: n normal   l loopback   s send one now   e error counters   ? status
 *
 * ----------------------------------------------------------------------------
 * HOW IT WORKS (register names from p33CK256MC005.h, fields from MCC data)
 *   Clock: CANCLKCON: CANCLKSEL = FVCO/4 = 400/4 = 100 MHz, CANCLKDIV = /5
 *          -> 20 MHz CAN clock.  (FVCO = 400 MHz in the core's 100 MIPS setup)
 *   Bit timing (C1NBTCFG): 500 kbit/s at 20 MHz = 40 time quanta per bit
 *          BRP = 0 (1 tq = 50 ns), TSEG1 = 30 (31 tq), TSEG2 = 7 (8 tq), SJW = 7
 *          1 + 31 + 8 = 40 tq, sample point (1+31)/40 = 80 %   (CiA recommends 87.5 %
 *          for long buses; 80 % is the usual dev-board value)
 *   Modes (C1CONH.REQOP / OPMOD): 4 configuration, 2 internal loopback,
 *          6 normal CAN 2.0, 0 normal CAN FD
 *   Message RAM: the controller keeps its FIFOs in normal data RAM. C1FIFOBA
 *          holds the start address; the TXQ comes first, then FIFO1, ...
 *          Each object: 2 header words (T0/R0 = ID, T1/R1 = DLC/IDE/RTR/FDF)
 *          + payload (+ 1 timestamp word on RX if RXTSEN). C1TXQUA / C1FIFOUA1
 *          tell the address of the object the CPU may use next; after writing
 *          or reading it, set UINC to move on (and TXREQ to transmit).
 *   NOT verified on hardware: whether CxFIFOUA holds an absolute RAM address or
 *          an offset from CxFIFOBA. The sketch accepts either (see objPtr()).
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>

#if FCY < 50000000UL
#error "Lab26 needs Tools > Clock = 100 MIPS (CAN clock = FVCO/4 from the PLL)"
#endif

const uint8_t CAN_TX_PIN = 27;     /* RC6 */
const uint8_t CAN_RX_PIN = 28;     /* RC7 */
const uint16_t MY_ID = 0x123;

/* ---------------- message RAM ----------------
 * TXQ: 4 objects x (2 header + 2 data words) = 4 x 4 x 32 bit
 * FIFO1 (RX): 8 objects x (2 header + 1 timestamp? no: RXTSEN=0 -> 2 + 2) */
#define TXQ_N   4
#define RX_N    8
#define OBJ_W   4                  /* 32-bit words per object: T0, T1, data[0..3], data[4..7] */
static uint32_t canRam[(TXQ_N + RX_N) * OBJ_W] __attribute__((aligned(4)));

volatile unsigned long rxCount = 0;
unsigned long txCount = 0, txFail = 0;
bool loopback = true;

/* ---------------- low level ---------------- */
static void ppsOut(int16_t rp, uint8_t fn)
{
    volatile uint16_t *rpor = &RPOR0 + ((rp - 32) / 2);
    if (rp & 1) *rpor = (uint16_t)((*rpor & 0x00FFu) | ((uint16_t)fn << 8));
    else        *rpor = (uint16_t)((*rpor & 0xFF00u) | fn);
}

/* CxFIFOUA -> CPU pointer. Absolute address if it lies inside canRam, else an
 * offset from the base (MCP2517FD convention). */
static volatile uint32_t *objPtr(uint16_t ua)
{
    uint16_t base = (uint16_t)canRam, end = (uint16_t)(canRam + (TXQ_N + RX_N) * OBJ_W);
    if (ua >= base && ua < end) return (volatile uint32_t *)ua;
    return (volatile uint32_t *)(base + ua);
}

static bool setMode(uint8_t mode)
{
    C1CONHbits.REQOP = mode;
    unsigned long t0 = millis();
    while (C1CONHbits.OPMOD != mode) if (millis() - t0 > 50) return false;
    return true;
}

bool canBegin(bool useLoopback)
{
    PMD1bits.C1MD = 0;                         /* power the module */

    /* CAN clock 20 MHz = FVCO/4 (100 MHz) / 5 */
    CANCLKCONbits.CANCLKEN = 0;
    CANCLKCONbits.CANCLKSEL = 5;               /* FVCO/4 */
    CANCLKCONbits.CANCLKDIV = 4;               /* divide by 5 */
    CANCLKCONbits.CANCLKEN = 1;

    /* pins: TX output via PPS (CAN1TX = 21), RX input via RPINR26 */
    int16_t rpTx = pinToRP(CAN_TX_PIN), rpRx = pinToRP(CAN_RX_PIN);
    digitalWrite(CAN_TX_PIN, HIGH);            /* recessive = 1 */
    pinMode(CAN_TX_PIN, OUTPUT);
    pinMode(CAN_RX_PIN, INPUT_PULLUP);         /* recessive when no transceiver */
    __builtin_write_RPCON(0x0000);
    ppsOut(rpTx, 21);
    _CAN1RXR = (uint16_t)rpRx;
    __builtin_write_RPCON(0x0800);

    C1CONLbits.CON = 1;                        /* module on (stays in config mode) */
    if (!setMode(4)) return false;             /* 4 = configuration mode */

    /* nominal bit timing: 500 kbit/s */
    C1NBTCFGH = (0u << 8) | 30u;               /* BRP = 0, TSEG1 = 30 */
    C1NBTCFGL = (7u << 8) | 7u;                /* TSEG2 = 7, SJW = 7  */
    C1DBTCFGH = (0u << 8) | 30u;               /* data phase = same (no FD bit-rate switch) */
    C1DBTCFGL = (7u << 8) | 7u;

    /* message RAM */
    uint16_t ba = (uint16_t)canRam;
    C1FIFOBAL = ba; C1FIFOBAH = 0;

    C1CONHbits.STEF = 0;                       /* no transmit event FIFO */
    C1CONHbits.TXQEN = 1;                      /* use the transmit queue */
    C1CONHbits.RTXAT = 1;                      /* honour TXAT retry limit */

    /* TXQ: 4 objects, 8-byte payload, 3 attempts */
    C1TXQCONH = (uint16_t)(((TXQ_N - 1) << 8) | (1u << 5) | 0u);   /* FSIZE, TXAT=01, PRI 0 */
    C1TXQCONL = 0x0080;                        /* TXEN must be 1 for the TXQ */

    /* FIFO1: receive, 8 objects, 8-byte payload, no timestamp */
    C1FIFOCON1H = (uint16_t)((RX_N - 1) << 8); /* FSIZE, PLSIZE = 8 bytes */
    C1FIFOCON1L = 0x0001;                      /* TXEN = 0 (receive), TFNRFNIE: not empty IRQ */

    /* filter 0 -> FIFO1: accept 0x100..0x1FF (and our own 0x123 in loopback) */
    C1FLTCON0L = 0;                            /* disable while changing */
    C1FLTOBJ0L = 0x100; C1FLTOBJ0H = 0;        /* SID = 0x100, standard frame */
    C1MASK0L   = 0x700; C1MASK0H = (1u << 14); /* compare SID bits 10..8, MIDE: match IDE */
    C1FLTCON0L = 0x0080 | 0x01;                /* FLTEN0, buffer pointer = FIFO1 */

    /* interrupt: any receive FIFO not empty */
    C1INTH = 0x0002;                           /* RXIE */
    IPC6bits.C1IP = 3;
    IFS1bits.C1IF = 0;
    IEC1bits.C1IE = 1;

    loopback = useLoopback;
    return setMode(useLoopback ? 2 : 6);       /* 2 internal loopback, 6 normal CAN 2.0 */
}

/* receive ring for the main loop */
struct Frame { uint16_t id; uint8_t dlc; uint8_t d[8]; };
#define RXQ 16
volatile Frame rxq[RXQ];
volatile uint8_t rqHead = 0, rqTail = 0;

extern "C" void __attribute__((interrupt, no_auto_psv)) _C1Interrupt(void)
{
    while (C1FIFOSTA1 & 0x0001) {              /* TFNRFNIF: FIFO1 not empty */
        volatile uint32_t *o = objPtr(C1FIFOUA1L);
        uint32_t r0 = o[0], r1 = o[1];
        uint32_t d0 = o[2], d1 = o[3];
        uint8_t next = (uint8_t)((rqHead + 1) % RXQ);
        if (next != rqTail) {
            volatile Frame &f = rxq[rqHead];
            f.id = (uint16_t)(r0 & 0x7FF);
            f.dlc = (uint8_t)(r1 & 0x0F);
            for (int i = 0; i < 4; i++) { f.d[i] = (uint8_t)(d0 >> (8 * i)); f.d[4 + i] = (uint8_t)(d1 >> (8 * i)); }
            rqHead = next;
        }
        rxCount++;
        C1FIFOCON1Lbits.UINC = 1;              /* release the object */
    }
    C1INTL = 0;                                /* clear module flags */
    IFS1bits.C1IF = 0;
}

bool canSend(uint16_t id, const uint8_t *data, uint8_t len)
{
    if (!(C1TXQSTA & 0x0001)) { txFail++; return false; }   /* TXQNIF: queue not full? */
    volatile uint32_t *o = objPtr(C1TXQUAL);
    uint32_t d0 = 0, d1 = 0;
    for (int i = 0; i < len && i < 8; i++) { if (i < 4) d0 |= (uint32_t)data[i] << (8 * i); else d1 |= (uint32_t)data[i] << (8 * (i - 4)); }
    o[0] = id & 0x7FF;                         /* T0: SID */
    o[1] = (uint32_t)(len & 0x0F);             /* T1: DLC, IDE=0, RTR=0, BRS=0, FDF=0 */
    o[2] = d0; o[3] = d1;
    C1TXQCONL |= 0x0100 | 0x0200;              /* UINC + TXREQ */
    txCount++;
    return true;
}

/* ---------------- demo ---------------- */
void status(void)
{
    static const char *const M[8] = { "CAN FD normal", "disabled", "INTERNAL LOOPBACK", "listen only",
                                      "configuration", "external loopback", "NORMAL CAN 2.0", "restricted" };
    Serial.print("# mode="); Serial.print(M[C1CONHbits.OPMOD]);
    Serial.print("  tx="); Serial.print(txCount); Serial.print(" (queue full "); Serial.print(txFail);
    Serial.print(")  rx="); Serial.print(rxCount);
    Serial.print("  TEC="); Serial.print(C1TRECL >> 8); Serial.print(" REC="); Serial.print(C1TRECL & 0xFF);
    uint16_t h = C1TRECH;
    Serial.println((h & 0x20) ? "  BUS-OFF" : (h & 0x18) ? "  error-passive" : (h & 0x01) ? "  warning" : "  error-active");
}

void sendNow(void)
{
    static uint8_t counter = 0;
    int a = analogRead(A0);
    uint8_t d[8] = { counter++, (uint8_t)(a >> 8), (uint8_t)a,
                     (uint8_t)(digitalRead(BUTTON_BUILTIN) == LOW), 0xCA, 0xFE, 0, 0 };
    canSend(MY_ID, d, 8);
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    Serial.println("Lab 26 - CAN 500 kbit/s. TX RC6 (J2-22), RX RC7 (J3-18). n l s e ?");
    if (!canBegin(true)) Serial.println("# CAN did not enter the requested mode!");
    status();
}

void loop()
{
    static unsigned long last = 0;
    if (millis() - last >= 500) { last = millis(); sendNow(); }

    while (rqTail != rqHead) {
        Frame f;
        f.id = rxq[rqTail].id; f.dlc = rxq[rqTail].dlc;
        for (int i = 0; i < 8; i++) f.d[i] = rxq[rqTail].d[i];
        rqTail = (uint8_t)((rqTail + 1) % RXQ);
        Serial.print("rx id=0x"); Serial.print(f.id, HEX); Serial.print(" dlc="); Serial.print(f.dlc); Serial.print(" :");
        for (int i = 0; i < f.dlc && i < 8; i++) { Serial.print(' '); if (f.d[i] < 16) Serial.print('0'); Serial.print(f.d[i], HEX); }
        Serial.println();
        if (f.id == 0x120 && f.dlc >= 1) digitalWrite(LED_BUILTIN, f.d[0] ? LED_ON : LED_OFF);
        else if (f.id == MY_ID) digitalWrite(LED_BUILTIN, (f.d[0] & 1) ? LED_ON : LED_OFF);
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'n')      { setMode(4); setMode(6); loopback = false; Serial.println("# NORMAL mode - needs another node to ACK"); status(); }
        else if (c == 'l') { setMode(4); setMode(2); loopback = true;  Serial.println("# internal LOOPBACK"); status(); }
        else if (c == 's') sendNow();
        else if (c == 'e' || c == '?') status();
    }
}
