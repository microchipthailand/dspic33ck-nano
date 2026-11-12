/*
 * Lab 21 - GPS data logger (NMEA parser on UART2)
 * Board: dsPIC33CK Nano
 *
 * Goal:  read a GPS module on a second hardware UART, parse NMEA sentences,
 *        check their checksum, and log position/time as CSV on Serial (J5).
 *
 * Parts: any 3.3 V-logic NMEA GPS at 9600 baud
 *        (u-blox NEO-6M/7M/M8N breakout, Quectel L80, ATGM336H ...)
 *
 * Wiring (breadboard):
 *   GPS VCC -> 3V3 (J3-1)   (or 5V J2-1 if the module has its own regulator
 *                            AND its TX pin is 3.3 V - check its datasheet)
 *   GPS GND -> GND (J3-2)
 *   GPS TX  -> RB10 = D15 (J2-4)   <- Serial2 RX
 *   GPS RX  -> RB11 = D16 (J2-5)   -> Serial2 TX (optional, only to configure)
 *   USB-serial adapter on J5 for the log output (115200).
 *
 * LED:   off = no data from GPS, slow blink = data but no fix, solid = fix.
 * USER:  press to start/stop logging. Each logged row:
 *        utc,date,lat,lon,fix,sats,hdop,alt_m,speed_kmh
 *
 * NMEA sentences used:
 *   $GPRMC / $GNRMC  time, status A/V, lat, lon, speed (knots), date
 *   $GPGGA / $GNGGA  time, lat, lon, fix quality, satellites, HDOP, altitude
 */

#include <Arduino.h>
#include <NanoSerial2.h>
#include <string.h>
#include <stdlib.h>

const unsigned long GPS_BAUD = 9600;
const unsigned long LOG_PERIOD_MS = 1000;

/* ---------- parsed state ---------- */
struct GpsData {
    char   utc[11];       /* hhmmss.ss */
    char   date[7];       /* ddmmyy */
    double lat, lon;      /* decimal degrees, + = N / E */
    bool   valid;         /* RMC status 'A' */
    int    fixQuality;    /* GGA: 0 none, 1 GPS, 2 DGPS ... */
    int    sats;
    double hdop, altM, speedKmh;
    unsigned long lastSentenceMs;
};
GpsData gps;

/* ---------- NMEA line assembly ---------- */
char line[96];
uint8_t lineLen = 0;
unsigned long goodSentences = 0, badChecksums = 0;

/* "ddmm.mmmm" + hemisphere -> signed decimal degrees */
double nmeaToDeg(const char *v, char hemi)
{
    if (!v[0]) return 0.0;
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double minutes = raw - deg * 100.0;
    double d = deg + minutes / 60.0;
    return (hemi == 'S' || hemi == 'W') ? -d : d;
}

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* XOR of everything between '$' and '*' must equal the two hex digits after '*' */
bool checksumOk(const char *s)
{
    if (s[0] != '$') return false;
    uint8_t x = 0;
    const char *p = s + 1;
    while (*p && *p != '*') x ^= (uint8_t)*p++;
    if (*p != '*' || hexVal(p[1]) < 0 || hexVal(p[2]) < 0) return false;
    return x == (uint8_t)(hexVal(p[1]) * 16 + hexVal(p[2]));
}

/* split on ',' in place, keeping empty fields; returns field count */
int splitFields(char *s, char *f[], int maxF)
{
    int n = 0;
    char *star = strchr(s, '*');
    if (star) *star = 0;
    f[n++] = s;
    while (*s && n < maxF) {
        if (*s == ',') { *s = 0; f[n++] = s + 1; }
        s++;
    }
    return n;
}

void parseSentence(char *s)
{
    if (!checksumOk(s)) { badChecksums++; return; }
    goodSentences++;
    gps.lastSentenceMs = millis();

    char *f[20];
    int n = splitFields(s, f, 20);
    const char *type = f[0] + 3;             /* skip "$GP" / "$GN" talker */

    if (strcmp(type, "RMC") == 0 && n >= 10) {
        strncpy(gps.utc, f[1], sizeof(gps.utc) - 1);
        gps.valid = (f[2][0] == 'A');
        gps.lat = nmeaToDeg(f[3], f[4][0]);
        gps.lon = nmeaToDeg(f[5], f[6][0]);
        gps.speedKmh = atof(f[7]) * 1.852;   /* knots -> km/h */
        strncpy(gps.date, f[9], sizeof(gps.date) - 1);
    } else if (strcmp(type, "GGA") == 0 && n >= 10) {
        gps.fixQuality = atoi(f[6]);
        gps.sats = atoi(f[7]);
        gps.hdop = atof(f[8]);
        gps.altM = atof(f[9]);
    }
}

void feedGps(void)
{
    while (Serial2.available() > 0) {
        char c = (char)Serial2.read();
        if (c == '$') lineLen = 0;            /* resynchronise on every start */
        if (c == '\r' || c == '\n') {
            if (lineLen > 6) { line[lineLen] = 0; parseSentence(line); }
            lineLen = 0;
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
        } else {
            lineLen = 0;                       /* too long: drop it */
        }
    }
}

void printRow(void)
{
    Serial.print(gps.utc[0] ? gps.utc : "-");  Serial.print(",");
    Serial.print(gps.date[0] ? gps.date : "-"); Serial.print(",");
    Serial.print(gps.lat, 6);  Serial.print(",");
    Serial.print(gps.lon, 6);  Serial.print(",");
    Serial.print(gps.valid ? gps.fixQuality : 0); Serial.print(",");
    Serial.print(gps.sats);    Serial.print(",");
    Serial.print(gps.hdop, 1); Serial.print(",");
    Serial.print(gps.altM, 1); Serial.print(",");
    Serial.println(gps.speedKmh, 1);
}

bool logging = true;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);
    pinMode(BUTTON_BUILTIN, INPUT);
    memset(&gps, 0, sizeof(gps));

    Serial.begin(115200);
    Serial2.begin(GPS_BAUD);                  /* RX RB10 (J2-4), TX RB11 (J2-5) */

    Serial.println("# Lab 21 - GPS logger. USER = start/stop logging");
    Serial.println("utc,date,lat,lon,fix,sats,hdop,alt_m,speed_kmh");
}

void loop()
{
    feedGps();

    /* USER button: toggle logging on press */
    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        logging = !logging;
        Serial.println(logging ? "# logging ON" : "# logging OFF");
        delay(30);
    }
    lastBtn = btn;

    /* status LED */
    bool hearing = (millis() - gps.lastSentenceMs) < 3000 && goodSentences > 0;
    bool fix = hearing && gps.valid && gps.fixQuality > 0;
    if (fix)          digitalWrite(LED_BUILTIN, LED_ON);
    else if (hearing) digitalWrite(LED_BUILTIN, (millis() / 500) % 2 ? LED_ON : LED_OFF);
    else              digitalWrite(LED_BUILTIN, LED_OFF);

    /* one CSV row per second */
    static unsigned long lastLog = 0;
    if (logging && millis() - lastLog >= LOG_PERIOD_MS) {
        lastLog = millis();
        if (!hearing) {
            Serial.print("# no GPS data - check GPS TX -> J2-4 and 9600 baud. good=");
            Serial.print(goodSentences);
            Serial.print(" bad=");
            Serial.println(badChecksums);
        } else {
            printRow();
        }
    }
}
