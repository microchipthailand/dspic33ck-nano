/*
 * DSP.h - fixed-point DSP on the dsPIC33CK DSP engine
 *
 *   q15 a = Q15(0.5);                    // fractional number -1 .. +0.99997
 *   q15 p = q15mul(a, a);                // 0.25
 *   q15 d = dspDot(x, y, n);             // sum x[i]*y[i] with the MAC unit
 *   Fir f; firInit(f, taps, delayBuf, n); q15 out = firRun(f, in);
 *   float m = goertzelPower(x, n, 697, 8000);    // strength of one frequency
 *
 * ----------------------------------------------------------------------------
 * Q15 FIXED POINT
 *   An int16_t read as a fraction: value = int / 32768.
 *     0x7FFF = +0.99997   0x4000 = +0.5   0x0000 = 0   0xC000 = -0.5   0x8000 = -1
 *   Multiplying two Q15 numbers gives a Q30 product (32 bits); shift right 15
 *   to get Q15 back. Adding can overflow past +1: plain C wraps around to a
 *   negative number, the DSP engine can SATURATE (stick at +0.99997) instead.
 *
 * THE DSP ENGINE (what makes it a "dsPIC")
 *   two 40-bit accumulators A and B: 32 bits + 8 guard bits, so a sum of up
 *     to 256 full-scale products cannot overflow
 *   a 17x17 multiplier, fractional mode: the product is already shifted <<1
 *   MAC = multiply + accumulate in ONE instruction cycle (plus two operand
 *     fetches for the next step in the same cycle)
 *   CORCON register: SATA / SATB accumulator saturation, SATDW saturate when
 *     storing, ACCSAT 1.31 or 9.31 saturation, RND rounding mode, IF integer /
 *     fractional multiply
 *   The compiler gives access through builtins:
 *     acc = __builtin_clr();                          A = 0
 *     acc = __builtin_mac(acc, x, y, 0,0,0, 0,0,0, 0,0);   A += x*y
 *     acc = __builtin_lac(v, shift);                  A = v (load)
 *     acc = __builtin_add(acc, v, shift);             A += v
 *     r = __builtin_sacr(acc, shift);                 store, rounded, saturated
 *   with the accumulator declared as   volatile register int acc asm("A");
 *   ("volatile" matters: without it the compiler may lose track of the
 *   accumulator and stop with "Expected DSP accumulator result".)
 *
 *   A MAC in a C loop still costs ~10 cycles (loads, loop counter). The
 *   hardware's real speed needs REPEAT + MAC with PREFETCH: the MAC fetches
 *   the next x through W8 (X bus) and the next y through W10 (Y bus) in the
 *   same cycle -> 1 cycle per tap. The y operand must then be in Y memory
 *   (0x3000-0x4FFF here): declare it with DSP_YMEM. See dspDotFast().
 *
 * FIR FILTER   y[n] = h[0]x[n] + h[1]x[n-1] + ... + h[N-1]x[n-N+1]
 *   = a dot product of the coefficients with the last N inputs: N MACs.
 * GOERTZEL     the energy at ONE frequency (one DFT bin) - cheaper than a
 *   whole FFT when you want only a few tones (e.g. DTMF telephone keys).
 * ----------------------------------------------------------------------------
 */
#ifndef LAB36_DSP_H
#define LAB36_DSP_H

#include <Arduino.h>
#include <math.h>

/* "volatile register ... asm("A")" is the documented way to name an
 * accumulator; GCC warns that it is unusual. It is intended here. */
#pragma GCC diagnostic ignored "-Wvolatile-register-var"

typedef int16_t q15;

#define Q15(x) ((q15)((x) >= 0.99997 ? 32767 : ((x) <= -1.0 ? -32768 : (int)((x) * 32768.0 + ((x) >= 0 ? 0.5 : -0.5)))))
inline float q15ToFloat(q15 v) { return v / 32768.0f; }
inline q15 floatToQ15(float f)
{
    if (f >= 0.99997f) return 32767;
    if (f <= -1.0f) return -32768;
    return (q15)lrintf(f * 32768.0f);
}

/* ---------------- CORCON: the DSP engine mode ---------------- */
inline void dspSaturation(bool on)
{
    CORCONbits.SATA = on;                   /* accumulator A saturates at +-1 (1.31) */
    CORCONbits.SATB = on;
    CORCONbits.SATDW = 1;                   /* storing an accumulator saturates */
    CORCONbits.ACCSAT = 0;                  /* 1.31 saturation */
}

/* ---------------- basic Q15 math ---------------- */
inline q15 q15mul(q15 a, q15 b) { return (q15)(__builtin_mulss(a, b) >> 15); }   /* truncates */
inline q15 q15addWrap(q15 a, q15 b) { return (q15)((uint16_t)a + (uint16_t)b); } /* plain C: wraps */

/* a + b in the accumulator. Saturates while adding if SATA is on, and on
 * the final store if SATDW is on. */
q15 q15addSat(q15 a, q15 b)
{
    volatile register int acc asm("A");
    acc = __builtin_lac(a, 0);
    acc = __builtin_add(acc, b, 0);
    return __builtin_sacr(acc, 0);
}

/* ---------------- dot product: the heart of every filter ---------------- */
/* Y data space: the second operand bus of the MAC unit (p33CK256MC005.gld) */
#define DSP_YMEM_START 0x3000U
#define DSP_YMEM_END   0x5000U
#define DSP_YMEM       __attribute__((space(ymemory)))   /* put a buffer there */
inline bool dspInYmem(const void *p, uint16_t n)
{
    uint16_t a = (uint16_t)p;
    return a >= DSP_YMEM_START && a + 2 * n <= DSP_YMEM_END;
}

/* Keep SATA off (the default) while summing: a partial sum may pass +-1 for a
 * moment, the guard bits hold it, and only the final store saturates (SATDW). */
q15 dspDot(const q15 *x, const q15 *y, uint16_t n)
{
    volatile register int acc asm("A");
    acc = __builtin_clr();
    for (uint16_t i = 0; i < n; i++)
        acc = __builtin_mac(acc, x[i], y[i], 0, 0, 0, 0, 0, 0, 0, 0);
    return __builtin_sacr(acc, 0);          /* round + saturate back to Q15 */
}

/* The real thing: REPEAT + MAC with operand prefetch. Every MAC multiplies
 * the pair fetched by the previous one and fetches the next pair at the same
 * time: one tap per instruction cycle. x can be anywhere in RAM (X bus, W8),
 * y MUST be in Y memory (Y bus, W10) - a Y fetch from outside it is an
 * address error trap. n >= 2. */
q15 dspDotFast(const q15 *x, const q15 *y, uint16_t n)
{
    if (n < 2 || !dspInYmem(y, n)) return dspDot(x, y, n);     /* safe fallback */
    q15 r;
    __asm__ volatile (
        "mov    %1, w8\n\t"
        "mov    %2, w10\n\t"
        "sub    %3, #2, w0\n\t"
        "clr    A, [w8]+=2, w4, [w10]+=2, w6\n\t"             /* A = 0, fetch x0, y0 */
        "repeat w0\n\t"                                       /* next line n-1 times */
        "mac    w4*w6, A, [w8]+=2, w4, [w10]+=2, w6\n\t"      /* A += x*y, fetch next */
        "mac    w4*w6, A\n\t"                                 /* last pair */
        "sac.r  A, %0"                                        /* round, saturate, store */
        : "=r"(r) : "r"(x), "r"(y), "r"(n) : "w0", "w4", "w6", "w8", "w10");
    return r;
}

/* The same in plain C with 32-bit integers (no 40-bit guard: can overflow) */
q15 cDot(const q15 *x, const q15 *y, uint16_t n)
{
    int32_t s = 0;
    for (uint16_t i = 0; i < n; i++) s += (int32_t)x[i] * y[i];
    s = (s + 0x4000) >> 15;
    return (q15)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
}

/* And in float, for comparison (no FPU on this chip: done in software) */
float fDot(const float *x, const float *y, uint16_t n)
{
    float s = 0;
    for (uint16_t i = 0; i < n; i++) s += x[i] * y[i];
    return s;
}

/* ---------------- FIR filter ---------------- */
struct Fir {
    const q15 *h;                           /* coefficients h[0..n-1] */
    q15 *x;                                 /* delay line, 2*n entries */
    uint16_t n, pos;
};

/* delay must hold 2*n samples: every input is written twice (pos and pos+n)
 * so the last n inputs are always one straight block -> one MAC loop, no
 * wrap-around test inside it. Declare it DSP_YMEM to get the fast MAC loop:
 *   q15 delay[2 * N] DSP_YMEM;      */
void firInit(Fir &f, const q15 *h, q15 *delay, uint16_t n)
{
    f.h = h; f.x = delay; f.n = n; f.pos = 0;
    for (uint16_t i = 0; i < 2 * n; i++) delay[i] = 0;
}

q15 firRun(Fir &f, q15 in)
{
    f.pos = (f.pos == 0) ? f.n - 1 : f.pos - 1;     /* newest sample goes "in front" */
    f.x[f.pos] = in;
    f.x[f.pos + f.n] = in;
    return dspDotFast(f.h, &f.x[f.pos], f.n);       /* h[k] * x[n-k] */
}

/* Windowed-sinc low-pass design: n taps (odd), cutoff fc at sample rate fs.
 * Hamming window, scaled for a gain of 1 at DC. Done once, in float. */
void firDesignLowpass(q15 *h, uint16_t n, float fc, float fs)
{
    float w[129];
    float sum = 0, m = (n - 1) / 2.0f, wc = 2.0f * (float)M_PI * fc / fs;
    for (uint16_t i = 0; i < n && i < 129; i++) {
        float k = i - m;
        float sinc = (k == 0) ? wc / (float)M_PI : sinf(wc * k) / ((float)M_PI * k);
        float win = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (n - 1));
        w[i] = sinc * win;
        sum += w[i];
    }
    for (uint16_t i = 0; i < n && i < 129; i++) h[i] = floatToQ15(w[i] / sum * 0.999f);
}

/* ---------------- Goertzel: power at one frequency ---------------- */
float goertzelPower(const q15 *x, uint16_t n, float f, float fs)
{
    float coeff = 2.0f * cosf(2.0f * (float)M_PI * f / fs);
    float s1 = 0, s2 = 0;
    for (uint16_t i = 0; i < n; i++) {
        float s = q15ToFloat(x[i]) + coeff * s1 - s2;
        s2 = s1; s1 = s;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((float)n * n / 4.0f);   /* ~amplitude^2 */
}

/* ---------------- RMS with the MAC unit ---------------- */
/* n <= 256. The sum of squares may grow past 1.0: the 8 guard bits of the
 * accumulator hold it (SATA must be off, the default), and the store shifts
 * it right by 7 (the largest shift SAC allows) so it fits 32 bits again.
 * Limit: mean square <= 0.5 (e.g. a sine up to amplitude 1). */
q15 dspRms(const q15 *x, uint16_t n)
{
    volatile register int acc asm("A");
    acc = __builtin_clr();
    for (uint16_t i = 0; i < n; i++)
        acc = __builtin_mac(acc, x[i], x[i], 0, 0, 0, 0, 0, 0, 0, 0);
    int32_t sumsq = __builtin_sacd(acc, 7); /* (sum of squares) / 128, Q31 */
    float meanSq = (float)sumsq * 128.0f / 2147483648.0f / n;
    return floatToQ15(sqrtf(meanSq));
}

#endif
