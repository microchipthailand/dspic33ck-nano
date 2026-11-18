"""
gen_wiring_periph.py - breadboard wiring pictures for the peripheral labs 31-37.

usage:  python gen_wiring_periph.py <pcb_scripts dir> <out dir>
        -> <out>/lab31.svg .. lab37.svg and <out>/wiring_periph.json
then:   python embed_wiring.py dsPIC33CK_Nano_Peripheral_Labs.html <out>/wiring_periph.json

Uses the drawing functions of gen_wiring.py (same look, same netlist checks:
every wire names a signal and the header pin is looked up in design.py).
"""
import json, os, sys

import gen_wiring as G
from gen_wiring import (COL0, COLS, PX, R1K, R10K, board_xy, build, jumper_same_side, led, module,
                        pin_of, pot_on, power_rails, resistor, tag, wire)

J5NOTE = "USB-serial adapter on J5 (end of the board): J5-2 TX → adapter RX, J5-3 RX ← adapter TX, J5-4 GND. Leave J5-1 (3V3) open."
RIGHT_FREE = COL0 + 22 + 3          # first free breadboard column right of the board


def usb_serial(s):
    L = s.L
    x = L.ox + (COLS - 1) * PX - 160; y = L.bb_bottom() + 52
    m = module(s, x, y, 170, 52, "USB-serial 3.3 V", "CP2102 / CH340 / FT232", "#37474f", ["GND", "TX", "RX", "3V3"], side="T")
    j5 = [board_xy(L, 11.89 + k * 2.54, 71.7) for k in range(4)]   # J5-1..4
    for k, (jp, tgt, col) in enumerate(((1, "RX", "green"), (2, "TX", "yellow"), (3, "GND", "black"))):
        sx, sy = j5[jp]; ex, ey = m[tgt]
        mid = L.bb_bottom() + 18 + k * 8; sx2 = sx + 28 + k * 6
        wire(s, [(sx, sy), (sx2, sy), (sx2, mid), (ex, mid), (ex, ey)], col, 2.6)


def led_bottom(s, sig, c, color, label, optional=False, wire_col="orange", yoff=10):
    """signal -> 1k -> LED -> bottom - rail, parts in the bottom half at columns c..c+4"""
    L = s.L
    t = L.tap(sig)
    h, _ = pin_of(sig)
    if h == "J2":       # come round the right end of the breadboard
        right = L.ox + (COLS - 1) * PX + 16 + yoff / 2
        wire(s, [t, (t[0], L.bb_top() - yoff), (right, L.bb_top() - yoff), (right, L.bb_bottom() + yoff),
                 (L.hole(c, "j")[0], L.bb_bottom() + yoff), L.hole(c, "j")], wire_col, 2.6, optional=optional)
    else:
        wire(s, [t, (t[0], L.bb_bottom() + yoff), (L.hole(c, "j")[0], L.bb_bottom() + yoff), L.hole(c, "j")], wire_col, optional=optional)
    resistor(s, L.hole(c, "h"), L.hole(c + 3, "h"), "1k", R1K)
    led(s, L.hole(c + 3, "f"), L.hole(c + 4, "f"), color, label)
    wire(s, [L.hole(c + 4, "j"), L.rail("B-", c + 4)], "black")


def led_top(s, sig, c, color, label, yy):
    """J2 signal -> 1k -> LED -> top - rail, parts in the top half (as in Lab 16)"""
    L = s.L
    t = L.tap(sig)
    wire(s, [t, (t[0], yy), (L.hole(c, "a")[0], yy), L.hole(c, "a")], "orange", 2.6)
    resistor(s, L.hole(c, "c"), L.hole(c + 3, "c"), "1k", R1K)
    led(s, L.hole(c + 3, "e"), L.hole(c + 4, "e"), color, label)
    wire(s, [L.hole(c + 4, "a"), L.rail("T-", c + 4)], "black", 2.6)


def probe(s, sig, text, x_off=300, y_off=40):
    L = s.L
    t = L.tap(sig)
    s.add('<path d="M%.1f,%.1f l-5,16 l10,0 z" fill="#78909c" stroke="#37474f"/>' % (t[0], t[1] + 3), "top")
    tag(s, L.ox + x_off, L.bb_bottom() + y_off, text, "#6a1b9a")


def LABS_PERIPH():
    D = {}

    def d31(s):
        power_rails(s)
        pot_on(s, "RA0", RIGHT_FREE + 2)
        jumper_same_side(s, "RB3", "RB2", "green", 0.0, "jumper D8 J3-16 → D7 J3-17 (test tone → input D)")
        probe(s, "RB4", "grey probe (optional): comparator output on D9 (J3-15)", x_off=760, y_off=64)
    D["lab31"] = dict(title="Lab 31: pot on CMP1A, test-tone jumper D8 → D7, comparator output on D9", draw=d31,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"),
                             ("pot wiper = comparator input A", "RA0", "yellow", None),
                             ("jumper from D8 (test tone)", "RB3", "green", None), ("jumper to D7 = comparator input D", "RB2", "green", None),
                             ("comparator output via PPS (scope, optional)", "RB4", "grey", None)],
                      notes=["the frequency test (g, d, n) needs the jumper; the threshold and SAR tests need only the pot"], bottom=150)

    def d32(s):
        L = s.L
        power_rails(s, need_3v3=False)
        led_bottom(s, "RB4", RIGHT_FREE + 2, "#e53935", "LED on D9 (opt.)", optional=True)
        t = L.tap("RB3")
        c = RIGHT_FREE + 10
        wire(s, [t, (t[0], L.bb_bottom() + 22), (L.hole(c, "j")[0], L.bb_bottom() + 22), L.hole(c, "j")], "purple", 2.6)
        tag(s, L.ox + 720, L.bb_bottom() + 40, "loose wire from D8: touch it to the - rail = INT1 'press' (after 'i')", "#6a1b9a")
    D["lab32"] = dict(title="Lab 32: optional LED on D9, a loose wire on D8 for the moved INT1", draw=d32,
                      conns=[("PWM via PPS → 1k → LED (optional)", "RB4", "orange", None),
                             ("INT1 test wire (touch GND)", "RB3", "purple", None), ("GND to - rail", "GND", "black", "J3")],
                      notes=["the USER LED and USER button are used too; 'f' and 'u' need no wires at all (virtual pins)"], bottom=120)

    def d33(s):
        power_rails(s, need_3v3=False)
        led_bottom(s, "RB4", RIGHT_FREE + 2, "#1e88e5", "LED / scope (opt.)", optional=True)
        probe(s, "RB4", "grey probe: PTG output 25 on D9 (J3-15), 1 kHz in program 4", x_off=420, y_off=60)
    D["lab33"] = dict(title="Lab 33: PTG output 25 on D9 (optional LED / scope)", draw=d33,
                      conns=[("PTG output 25 via PPS → 1k → LED (optional)", "RB4", "orange", None), ("GND to - rail", "GND", "black", "J3")],
                      notes=["programs 1-3 use only the USER LED and USER button"], bottom=120)

    def d_serial(s): usb_serial(s)
    D["lab34"] = dict(title="Lab 34: nothing to wire (USB-serial on J5)", draw=d_serial, conns=[], notes=[J5NOTE], bottom=130)
    D["lab35"] = dict(title="Lab 35: nothing to wire (USB-serial on J5, PICkit on J4 for the upload)", draw=d_serial, conns=[],
                      notes=[J5NOTE, "upload with a PICkit on J4 (Bootloader: None) so the DMT config words are programmed"], bottom=130)

    def d36(s):
        power_rails(s)
        pot_on(s, "RA0", RIGHT_FREE + 2, optional=True)
    D["lab36"] = dict(title="Lab 36: optional pot on A0 for the live filter", draw=d36,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"),
                             ("pot wiper (optional, command 'a')", "RA0", "yellow", None)],
                      notes=["everything else in this lab uses test signals made in software"], bottom=110)

    def d37(s):
        L = s.L
        power_rails(s, need_3v3=False)
        cols = ["#e53935", "#43a047", "#1e88e5"]
        for k, (sig, nm) in enumerate((("RB14", "ch1"), ("RB12", "ch3"), ("RB10", "ch5"))):
            led_top(s, sig, RIGHT_FREE + 2 + k * 7, cols[k], "LED " + nm, L.bb_top() - 8 - k * 6)
        a = L.tap("GND", "J2"); r = L.rail("T-", COL0 + 2)
        wire(s, [a, (a[0], r[1])], "black")
        for k, (sig, nm) in enumerate((("RB15", "ch2"), ("RB13", "ch4"), ("RB11", "ch6"))):
            led_bottom(s, sig, RIGHT_FREE + 2 + k * 7, cols[k], "LED " + nm, yoff=30 + k * 7)
    D["lab37"] = dict(title="Lab 37: six LEDs (or a logic analyser) on PWM1H/L, PWM2H/L, PWM3H/L", draw=d37, top=80,
                      conns=[("ch1 PWM1H → 1k → LED", "RB14", "orange", None), ("ch2 PWM1L → 1k → LED", "RB15", "orange", None),
                             ("ch3 PWM2H → 1k → LED", "RB12", "orange", None), ("ch4 PWM2L → 1k → LED", "RB13", "orange", None),
                             ("ch5 PWM3H → 1k → LED", "RB10", "orange", None), ("ch6 PWM3L → 1k → LED", "RB11", "orange", None),
                             ("GND to top - rail", "GND", "black", "J2"), ("GND to bottom - rail", "GND", "black", "J3")],
                      notes=["dither test (optional): J2-8 → 10k → node → 1 uF → GND, node → A0 (J2-15)"], bottom=170)
    def d38(s):
        L = s.L
        power_rails(s)
        pot_on(s, "RA0", RIGHT_FREE + 2)
        # optional 1.65 V divider for ANN0 = RD13 (J2-13): node strip c, 10k to the + rail and 10k to the - rail
        c = RIGHT_FREE + 12
        t = L.tap("RD13")
        right = L.ox + (COLS - 1) * PX + 24
        wire(s, [t, (t[0], L.bb_top() - 16), (right, L.bb_top() - 16), (right, L.bb_bottom() + 22),
                 (L.hole(c, "j")[0], L.bb_bottom() + 22), L.hole(c, "j")], "blue", 2.6, optional=True)
        resistor(s, L.hole(c, "g"), L.hole(c + 3, "g"), "10k", R10K)
        wire(s, [L.hole(c + 3, "j"), L.rail("B+", c + 3)], "red")
        resistor(s, L.hole(c - 3, "h"), L.hole(c, "h"), "10k", R10K)
        wire(s, [L.hole(c - 3, "j"), L.rail("B-", c - 3)], "black")
        tag(s, L.hole(c, "j")[0], L.bb_bottom() + 44, "optional: 1.65 V on ANN0 (RD13) for the differential test 'd'", "#1565c0")
    D["lab38"] = dict(title="Lab 38: pot on AN0, optional 1.65 V divider on ANN0 (RD13)", draw=d38, top=60,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"),
                             ("pot wiper = AN0", "RA0", "yellow", None),
                             ("ANN0 → divider node (optional)", "RD13", "blue", None)],
                      notes=["divider: node → 10k → + rail (3V3), node → 10k → - rail (GND)",
                             "any other analog pin (table in the lab) can take a second pot"], bottom=140)
    return D


def main():
    out = G.OUT
    os.makedirs(out, exist_ok=True)
    res = {}
    for key, spec in LABS_PERIPH().items():
        svg = build(spec)
        res[key] = svg
        with open(os.path.join(out, key + ".svg"), "w", encoding="utf-8") as f:
            f.write(svg)
    with open(os.path.join(out, "wiring_periph.json"), "w", encoding="utf-8") as f:
        json.dump(res, f)
    print("wrote", len(res), "diagrams to", out)


if __name__ == "__main__":
    main()
