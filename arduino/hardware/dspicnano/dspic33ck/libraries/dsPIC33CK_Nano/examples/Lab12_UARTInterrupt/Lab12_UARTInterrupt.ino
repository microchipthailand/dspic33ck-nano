/*
 * Lab 12 - UART with interrupts: RX + TX ring buffers, error interrupt
 * Board: dsPIC33CK Nano
 *
 * Goal:  write a complete interrupt-driven UART driver on the bare registers
 *        of UART3 - the third UART, which the core and Labs 21/24 don't touch.
 *          RX interrupt  -> bytes go into a ring buffer, nothing is ever lost
 *          TX interrupt  -> print() returns immediately; the ISR feeds the FIFO
 *          Error interrupt -> count framing / overrun errors
 *        Then compare: how long does printing 1 kB take with a blocking UART
 *        (Serial on J5) vs. the interrupt-driven one?
 *
 * Pins (PPS, any remappable pin works):
 *   U3TX = RC0 = D21 (J2-14)
 *   U3RX = RC1 = D22 (J2-20)
 *
 * Test 1 - loopback (no parts): jumper J2-14 -> J2-20. Type in the Serial
 *   Monitor (J5): the text goes out of UART3, comes back into UART3's RX
 *   interrupt and is shown with the byte count.
 * Test 2 - second terminal: remove the jumper, connect a 2nd USB-serial
 *   adapter: its RX -> J2-14, its TX -> J2-20, GND. Open it at 115200.
 *   What you type there appears in the Serial Monitor and vice versa.
 *
 * Commands in the Serial Monitor (J5):
 *   any text  -> sent out of UART3
 *   !         -> speed test: 1024 bytes via blocking Serial vs. UART3 IRQ
 *   #         -> statistics
 *
 * HOW IT WORKS (registers of the dsPIC33CK UART, names from p33CK256MC005.h)
 *   UxMODE   UARTEN, UTXEN, URXEN, MOD = 0000 (8N1 async)
 *   UxMODEH  BCLKMOD = 1 fractional baud generator: baud = FCY / UxBRG
 *   UxSTAH   URXISEL = 000: RX interrupt when the RX FIFO has >= 1 byte
 *            UTXISEL = 000: TX interrupt when the TX FIFO has >= 1 free slot
 *            (FIFO depth on this family is 8 bytes; the ISRs only rely on the
 *             FULL/EMPTY flags, so the exact depth does not matter)
 *   UxSTA    FERIE / OERIE: error interrupt on framing / overrun error
 *   IFS3/IEC3/IPC14  U3EIF U3RXIF U3TXIF, priority 3 (below millis() = 4)
 *   PPS      _RPOUT_U3TX = 27 (header), RPINR27.U3RXR = RP number
 *
 *   TX buffering: putc() writes into txBuf, then (with the TX interrupt
 *   masked) pushes what fits straight into the hardware FIFO and unmasks the
 *   interrupt only if bytes are still waiting. The ISR tops the FIFO up as it
 *   drains and DISABLES itself when txBuf is empty - otherwise a level-type
 *   "FIFO has room" interrupt would fire forever.
 */

#include <Arduino.h>

const unsigned long U3_BAUD = 115200;
const uint8_t U3_TX_PIN = 21;     /* RC0 = RP48 */
const uint8_t U3_RX_PIN = 22;     /* RC1 = RP49 */

/* ---------------- ring buffers ---------------- */
#define RXB 128                    /* power of two -> cheap modulo */
#define TXB 256
static volatile uint8_t  rxBuf[RXB], txBuf[TXB];
static volatile uint16_t rxHead, rxTail, txHead, txTail;
static volatile unsigned long rxCount, txCount, rxDropped, framingErr, overrunErr, txIsrCalls, rxIsrCalls;

/* ---------------- interrupt service routines ---------------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _U3RXInterrupt(void)
{
    rxIsrCalls++;
    while (!U3STAHbits.URXBE) {                  /* drain the hardware FIFO */
        uint8_t c = (uint8_t)U3RXREG;
        uint16_t next = (uint16_t)((rxHead + 1) & (RXB - 1));
        if (next != rxTail) { rxBuf[rxHead] = c; rxHead = next; rxCount++; }
        else rxDropped++;                        /* software buffer full */
    }
    IFS3bits.U3RXIF = 0;
}

/* move bytes from txBuf into the hardware FIFO while it has room */
static inline void txFill(void)
{
    while (!U3STAHbits.UTXBF && txTail != txHead) {
        U3TXREG = txBuf[txTail];
        txTail = (uint16_t)((txTail + 1) & (TXB - 1));
        txCount++;
    }
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _U3TXInterrupt(void)
{
    txIsrCalls++;
    IFS3bits.U3TXIF = 0;
    txFill();
    if (txTail == txHead) IEC3bits.U3TXIE = 0;   /* nothing left: stop interrupting */
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _U3EInterrupt(void)
{
    if (U3STAbits.FERR) framingErr++;
    if (U3STAbits.OERR) { overrunErr++; U3STAbits.OERR = 0; }   /* clearing OERR restarts RX */
    IFS3bits.U3EIF = 0;
}

/* ---------------- driver API ---------------- */
static void ppsOut(int16_t rp, uint8_t fn)
{
    volatile uint16_t *rpor = &RPOR0 + ((rp - 32) / 2);
    if (rp & 1) *rpor = (uint16_t)((*rpor & 0x00FFu) | ((uint16_t)fn << 8));
    else        *rpor = (uint16_t)((*rpor & 0xFF00u) | fn);
}

bool u3Begin(unsigned long baud)
{
    int16_t rpTx = pinToRP(U3_TX_PIN), rpRx = pinToRP(U3_RX_PIN);
    if (rpTx < 0 || rpRx < 0) return false;

    PMD3bits.U3MD = 0;                           /* power the module */
    pinMode(U3_RX_PIN, INPUT_PULLUP);            /* idle high if nothing connected */
    digitalWrite(U3_TX_PIN, HIGH);
    pinMode(U3_TX_PIN, OUTPUT);

    __builtin_write_RPCON(0x0000);               /* unlock PPS */
    ppsOut(rpTx, 27);                            /* _RPOUT_U3TX */
    _U3RXR = (uint16_t)rpRx;
    __builtin_write_RPCON(0x0800);               /* lock PPS */

    U3MODE = 0; U3MODEH = 0; U3STA = 0; U3STAH = 0;
    U3MODEHbits.BCLKSEL = 0;                     /* baud clock = FCY */
    U3MODEHbits.BCLKMOD = 1;                     /* fractional: baud = FCY / BRG */
    unsigned long brg = (FCY + baud / 2) / baud;
    U3BRG  = (uint16_t)(brg & 0xFFFF);
    U3BRGH = (uint16_t)((brg >> 16) & 0x000F);

    U3STAHbits.URXISEL = 0;                      /* RX IRQ: >= 1 byte waiting */
    U3STAHbits.UTXISEL = 0;                      /* TX IRQ: >= 1 free FIFO slot */
    U3STAbits.FERIE = 1;                         /* error IRQ sources */
    U3STAbits.OERIE = 1;

    rxHead = rxTail = txHead = txTail = 0;
    IPC14bits.U3RXIP = 3; IPC14bits.U3TXIP = 3; IPC14bits.U3EIP = 3;
    IFS3bits.U3RXIF = 0;  IFS3bits.U3TXIF = 0;  IFS3bits.U3EIF = 0;
    IEC3bits.U3RXIE = 1;  IEC3bits.U3EIE = 1;   /* TX IRQ only while there is data */

    U3MODEbits.UARTEN = 1;                       /* UARTEN first, then enables */
    U3MODEbits.UTXEN = 1;
    U3MODEbits.URXEN = 1;
    return true;
}

/* Safety net, called from loop() and while waiting for space: if bytes are
 * queued and the TX interrupt has not drained them, feed the FIFO by polling.
 * On real silicon the interrupt does the work and this finds nothing to do;
 * the MPLAB simulator does not raise the UART TX interrupt at all, and this
 * keeps the lab working there. txPolled counts how often it was needed. */
static volatile unsigned long txPolled = 0;
void u3Service(void)
{
    if (txTail != txHead && !U3STAHbits.UTXBF) {
        IEC3bits.U3TXIE = 0;
        uint16_t before = txTail;
        txFill();
        if (txTail != before) txPolled++;
        if (txTail != txHead) IEC3bits.U3TXIE = 1;
    }
}

/* queue one byte; waits only if the 256-byte software buffer is full */
void u3Putc(uint8_t c)
{
    uint16_t next = (uint16_t)((txHead + 1) & (TXB - 1));
    while (next == txTail) u3Service();          /* buffer full: wait for the ISR to drain it */
    txBuf[txHead] = c;
    txHead = next;
    /* Kick-start: with the TX interrupt masked, push what fits straight into
     * the hardware FIFO, then unmask. From then on the interrupt fires as the
     * FIFO drains. This works whether the TX interrupt is level- or
     * edge-triggered, so it does not depend on the exact UTXISEL meaning. */
    IEC3bits.U3TXIE = 0;
    txFill();
    if (txTail != txHead) IEC3bits.U3TXIE = 1;
}

void u3Print(const char *s) { while (*s) u3Putc((uint8_t)*s++); }
int  u3Available(void)      { return (int)((rxHead - rxTail) & (RXB - 1)); }
int  u3Read(void)
{
    if (rxHead == rxTail) return -1;
    uint8_t c = rxBuf[rxTail];
    rxTail = (uint16_t)((rxTail + 1) & (RXB - 1));
    return c;
}
bool u3TxIdle(void) { return txHead == txTail && U3STAbits.TRMT; }

/* ---------------- demo ---------------- */
void stats(void)
{
    Serial.print("# rx="); Serial.print(rxCount);
    Serial.print(" tx="); Serial.print(txCount);
    Serial.print(" rxISR="); Serial.print(rxIsrCalls);
    Serial.print(" txISR="); Serial.print(txIsrCalls);
    Serial.print(" dropped="); Serial.print(rxDropped);
    Serial.print(" framing="); Serial.print(framingErr);
    Serial.print(" overrun="); Serial.print(overrunErr);
    Serial.print(" txPolled="); Serial.println(txPolled);
    Serial.println(txIsrCalls ? "# TX path: interrupt driven" :
                   txCount   ? "# TX path: POLLED fallback - TX interrupt never fired" : "# TX path: nothing sent yet");
}

void speedTest(void)
{
    static char line[65];
    for (int i = 0; i < 64; i++) line[i] = (char)('A' + i % 26);
    line[62] = '\r'; line[63] = '\n'; line[64] = 0;

    /* blocking: Serial (UART1) - the CPU waits for every byte */
    unsigned long t0 = micros();
    for (int k = 0; k < 16; k++) Serial.print(line);
    unsigned long tBlock = micros() - t0;

    /* interrupt-driven: only the time to COPY into the buffer counts */
    while (!u3TxIdle()) u3Service();
    t0 = micros();
    for (int k = 0; k < 4; k++) u3Print(line);   /* 256 B = fits in txBuf */
    unsigned long tIrq = micros() - t0;
    unsigned long t1 = micros();
    while (!u3TxIdle()) u3Service();             /* how long the wire actually needs */
    unsigned long tWire = micros() - t1 + tIrq;

    Serial.print("# 1024 B blocking Serial : "); Serial.print(tBlock); Serial.println(" us of CPU time");
    Serial.print("# 256 B  UART3 + IRQ     : "); Serial.print(tIrq);
    Serial.print(" us of CPU time, "); Serial.print(tWire); Serial.println(" us on the wire");
    Serial.println("# with IRQs the CPU is free while the bytes go out");
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("Lab 12 - UART3 interrupt driver. TX RC0 (J2-14), RX RC1 (J2-20)");
    Serial.println("Jumper J2-14 -> J2-20, then type. '!' = speed test, '#' = stats");
    if (!u3Begin(U3_BAUD)) Serial.println("# UART3 pins are not remappable!");
    u3Print("Hello from UART3\r\n");   /* '#' then shows whether the TX IRQ or the polled fallback sent it */
}

void loop()
{
    u3Service();                                 /* no-op when the TX interrupt works */

    /* Serial Monitor -> UART3, with two local commands */
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c == '!') speedTest();
        else if (c == '#') stats();
        else u3Putc((uint8_t)c);
    }

    /* UART3 RX buffer -> Serial Monitor */
    static bool lineStart = true;
    while (u3Available() > 0) {
        int c = u3Read();
        if (lineStart) { Serial.print("u3 rx: "); lineStart = false; }
        Serial.write((uint8_t)c);
        if (c == '\n') lineStart = true;
        digitalWrite(LED_BUILTIN, LED_ON);
    }

    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 200) { lastBlink = millis(); digitalWrite(LED_BUILTIN, LED_OFF); }
}
