"""
gen_wiring.py - Arduino/Fritzing-style breadboard wiring pictures for every lab.

usage:  python gen_wiring.py <pcb_scripts dir> <out dir>
        -> <out>/labNN.svg (25 files) and <out>/wiring.json {"lab01": "<svg>", ...}

Correct by construction
-----------------------
* Header signals come from pcb_scripts/design.py (the PCB's own netlist).
  Every wire names a SIGNAL ("RA0", "+3V3", "GND" ...); the J2/J3 pin is looked
  up, so a wire cannot land on a pin that does not carry that signal.
* Breadboard geometry is real: 0.1" pitch, 5-hole strips, centre gap of 0.3".
  The Nano's headers are 0.9" apart, so J2 sits in row b and J3 in row i
  (b->e 0.3", gap 0.3", f->i 0.3"). Rows a and j stay free: a jumper in row a
  of column N is the same strip as J2 pin N, row j the same as J3 pin N.
* Other board parts (USB-C, buttons, LEDs, J4, J5, MCU) are placed from the
  KiCad PCB coordinates (mm), so the picture matches the real board.

Picture orientation: the board lies along the breadboard, USB-C on the LEFT,
J2 along the TOP, J3 along the BOTTOM, pin 1 of both headers at the USB end.
"""
import json, os, re, sys

sys.path.insert(0, sys.argv[1] if len(sys.argv) > 1 else str(__import__("pathlib").Path(__file__).resolve().parents[2] / "pcb" / "scripts"))
from design import LEFT, RIGHT

OUT = sys.argv[2] if len(sys.argv) > 2 else "wiring"
PX = 17.0                    # pixels per 0.1" (one breadboard hole)
MM = PX / 2.54               # pixels per mm
COLS = 60

# ---------------- breadboard rows (y in units of 0.1") ----------------
ROWS = {"T+": 0.0, "T-": 1.0,
        "a": 2.5, "b": 3.5, "c": 4.5, "d": 5.5, "e": 6.5,
        "f": 9.5, "g": 10.5, "h": 11.5, "i": 12.5, "j": 13.5,
        "B+": 15.0, "B-": 16.0}
BB_ROWS_H = 16.0
HDR_ROW = {"J2": "b", "J3": "i"}
FREE_ROW = {"J2": "a", "J3": "j"}
COL0 = 8                     # breadboard column (0-based) of header pin 1

# ---------------- netlist ----------------
def norm(s): return s.lstrip("+")
SIGS = {}
for i, n in enumerate(LEFT):  SIGS.setdefault(("J2", norm(n)), []).append(i + 1)
for i, n in enumerate(RIGHT): SIGS.setdefault(("J3", norm(n)), []).append(i + 1)

def pin_of(sig, prefer=None):
    s = norm(sig)
    hits = [(h, p) for (h, n), ps in SIGS.items() if n == s for p in ps]
    if not hits:
        raise KeyError("signal %r is not on J2 / J3" % sig)
    if prefer:
        hits = [x for x in hits if x[0] == prefer] or hits
    return sorted(hits)[0]

class Layout:
    def __init__(self, top_space, bottom_space):
        self.top = top_space; self.bottom = bottom_space
        self.ox = 30.0; self.oy = top_space
        self.w = self.ox + COLS * PX + 30
        self.h = self.oy + BB_ROWS_H * PX + 24 + bottom_space
    def hole(self, col, row):
        y = ROWS[row] if isinstance(row, str) else row
        return (self.ox + col * PX, self.oy + y * PX)
    def pin(self, h, p):                     # the header pin itself
        return self.hole(COL0 + p - 1, HDR_ROW[h])
    def tap(self, sig, prefer=None):         # free hole in the same strip as the header pin
        h, p = pin_of(sig, prefer)
        return self.hole(COL0 + p - 1, FREE_ROW[h])
    def rail(self, which, col):
        return self.hole(col, which)
    def bb_bottom(self): return self.oy + BB_ROWS_H * PX + 14
    def bb_top(self): return self.oy - 14

COLORS = {"red": "#d32f2f", "black": "#212121", "blue": "#1565c0", "green": "#2e7d32",
          "yellow": "#f2b600", "orange": "#ef6c00", "white": "#bdbdbd", "purple": "#7b1fa2",
          "brown": "#6d4c41", "cyan": "#00838f", "grey": "#757575", "pink": "#d81b60"}

def esc(t): return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

class Svg:
    def __init__(self, L): self.L = L; self.parts = []; self.wires = []; self.top = []
    def add(self, s, layer="parts"): getattr(self, layer).append(s)
    def text(self, x, y, t, size=10, anchor="middle", color="#222", weight="normal", rot=0,
             family="Segoe UI,Arial,sans-serif", layer="parts"):
        tr = ' transform="rotate(%d %.1f %.1f)"' % (rot, x, y) if rot else ""
        self.add('<text x="%.1f" y="%.1f" font-size="%.1f" text-anchor="%s" fill="%s" font-weight="%s" font-family="%s"%s>%s</text>'
                 % (x, y, size, anchor, color, weight, family, tr, esc(t)), layer)
    def render(self, title):
        L = self.L
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="100%%" style="max-width:%dpx" role="img" aria-label="%s">'
                % (L.w, L.h, L.w, esc(title)) + "".join(self.parts) + "".join(self.wires) + "".join(self.top) + "</svg>")

# ---------------- breadboard ----------------
def draw_breadboard(s):
    L = s.L
    x0, y0 = L.ox - 22, L.oy - 14
    w, h = (COLS - 1) * PX + 44, BB_ROWS_H * PX + 28
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="7" fill="#f5f2ea" stroke="#d8d1c2"/>' % (x0, y0, w, h))
    gy = L.oy + 8.0 * PX
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#e6e0d2"/>' % (x0 + 3, gy - 4, w - 6, 8))
    for r, col in (("T+", "#d32f2f"), ("T-", "#1565c0"), ("B+", "#d32f2f"), ("B-", "#1565c0")):
        _, y = L.hole(0, r)
        off = -PX * 0.5 if r.endswith("+") else PX * 0.5
        s.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="1.4"/>' % (L.ox - 4, y + off * 0.9, L.ox + (COLS - 1) * PX + 4, y + off * 0.9, col))
        s.text(L.ox - 13, y + 4, "+" if r.endswith("+") else "-", 11, color=col, weight="bold")
    for r in ROWS:
        for c in range(COLS):
            if r in ("T+", "T-", "B+", "B-") and c % 6 == 5: continue
            x, y = L.hole(c, r)
            s.add('<rect x="%.1f" y="%.1f" width="4.6" height="4.6" rx="1" fill="#b9b1a1"/>' % (x - 2.3, y - 2.3))
        if len(r) == 1:
            s.text(L.ox - 13, L.hole(0, r)[1] + 3, r, 7.5, color="#a49c8c")
    for c in range(0, COLS, 5):
        s.text(L.hole(c, 0)[0], L.oy + 1.85 * PX, str(c + 1), 6.5, color="#a49c8c")

# ---------------- the Nano board (from PCB coordinates) ----------------
BOARD_W_MM, BOARD_L_MM = 31.4, 74.5
J2_X_MM, J3_X_MM, PIN1_Y_MM = 4.27, 27.13, 10.0

def board_xy(L, xmm, ymm):
    """PCB (x across, y along) -> picture: along = picture x, across = picture y"""
    px0, py0 = L.pin("J2", 1)
    return (px0 + (ymm - PIN1_Y_MM) * MM, py0 + (xmm - J2_X_MM) * MM)

def draw_nano(s):
    L = s.L
    x0, y0 = board_xy(L, 0, 0)
    w, h = BOARD_L_MM * MM, BOARD_W_MM * MM
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" fill="#c62828" stroke="#8e0000" stroke-width="1.4"/>' % (x0, y0, w, h, 2.0 * MM))
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" fill="none" stroke="#ffffff22" stroke-width="3"/>' % (x0 + 2, y0 + 2, w - 4, h - 4, 2.0 * MM))
    # USB-C (J1 at x=15.7, y=3.4, 9 mm wide)
    ux, uy = board_xy(L, 15.7, 0)
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="3" fill="#cfd8dc" stroke="#607d8b"/>' % (ux - 2 * MM, uy - 4.6 * MM, 7.4 * MM, 9.2 * MM))
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="2" fill="#455a64"/>' % (ux - 1.2 * MM, uy - 3.2 * MM, 1.6 * MM, 6.4 * MM))
    s.text(ux + 7.2 * MM, uy + 3, "USB-C 5V", 7, color="#ffebee")
    # MCU (U1 at 15.7, 33.0, TQFP48 7x7 + leads 9x9)
    mx, my = board_xy(L, 15.7, 33.0)
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#9e9e9e"/>' % (mx - 4.5 * MM, my - 4.5 * MM, 9 * MM, 9 * MM))
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#263238"/>' % (mx - 3.5 * MM, my - 3.5 * MM, 7 * MM, 7 * MM))
    s.text(mx, my - 1, "dsPIC33CK", 6.5, color="#fff", weight="bold")
    s.text(mx, my + 7, "256MC005", 6, color="#b0bec5")
    # LDO, buttons, LEDs
    lx, ly = board_xy(L, 15.0, 15.6)
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#37474f"/>' % (lx - 2.2 * MM, ly - 2.5 * MM, 4.5 * MM, 5 * MM))
    for (bxm, bym, lab) in ((10.9, 47.0, "RESET"), (20.5, 47.0, "USER")):
        bx, by = board_xy(L, bxm, bym)
        s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="2" fill="#eceff1" stroke="#455a64"/><circle cx="%.1f" cy="%.1f" r="%.1f" fill="#546e7a"/>'
              % (bx - 2.1 * MM, by - 3.1 * MM, 4.2 * MM, 6.2 * MM, bx, by, 1.3 * MM))
        s.text(bx - 2.8 * MM, by + 3, lab, 6.6, color="#fff", weight="bold", anchor="end")
    for (dxm, dym, lab, col) in ((21.6, 20.8, "PWR", "#76ff03"), (11.0, 57.1, "USER LED", "#ffd600")):
        dx, dy = board_xy(L, dxm, dym)
        s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="#333" stroke-width=".5"/>' % (dx - 0.8 * MM, dy - 1.6 * MM, 1.6 * MM, 3.2 * MM, col))
        s.text(dx + 2.4 * MM, dy + 2.2, lab, 6, color="#fff", anchor="start")
    # J4 / J5 (on the board end, pins along PCB x)
    for ref, pads, ymm in (("J4 ICSP", 6, 66.6), ("J5 UART", 4, 71.7)):
        for k in range(pads):
            px, py = board_xy(L, 9.35 + k * 2.54 if ref.startswith("J4") else 11.89 + k * 2.54, ymm)
            s.add('<rect x="%.1f" y="%.1f" width="7" height="7" fill="#1b1b1b"/><rect x="%.1f" y="%.1f" width="3.6" height="3.6" fill="#ffca28"/>' % (px - 3.5, py - 3.5, px - 1.8, py - 1.8))
        tx, ty = board_xy(L, 6.0, ymm)
        s.text(tx, ty + 3, ref.split()[0], 6.5, color="#fff", weight="bold", anchor="end")
    jx, jy = board_xy(L, 11.89, 71.7)
    for k, lab in enumerate(("3V3", "TX", "RX", "GND")):
        px, py = board_xy(L, 11.89 + k * 2.54, 71.7)
        s.text(px + 6, py + 2.5, lab, 5.5, color="#ffebee", anchor="start")
    # header pins + labels
    for h, sigs in (("J2", LEFT), ("J3", RIGHT)):
        for i, n in enumerate(sigs):
            x, y = L.pin(h, i + 1)
            col = "#ff5252" if n in ("+5V", "+3V3") else "#90a4ae" if n == "GND" else "#ffca28"
            s.add('<rect x="%.1f" y="%.1f" width="8" height="8" fill="#1b1b1b"/><rect x="%.1f" y="%.1f" width="4" height="4" fill="%s"/>' % (x - 4, y - 4, x - 2, y - 2, col))
            lab = norm(n)
            if h == "J2": s.text(x + 2.5, y + 7.5, lab, 6.2, color="#fff", rot=90, anchor="start", family="Consolas,monospace")
            else:         s.text(x + 2.5, y - 7.5, lab, 6.2, color="#fff", rot=-90, anchor="start", family="Consolas,monospace")
        x, y = L.pin(h, 1)
        s.text(x - 9, y + 3, h, 7, color="#fff", weight="bold", anchor="end")

# ---------------- wires & parts ----------------
def smooth(pts, r=9.0):
    """polyline with rounded corners (radius r) - looks like a bent jumper wire"""
    import math
    if len(pts) < 3:
        return "M%.1f,%.1f L%.1f,%.1f" % (pts[0][0], pts[0][1], pts[-1][0], pts[-1][1])
    out = ["M%.1f,%.1f" % pts[0]]
    for i in range(1, len(pts) - 1):
        (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]
        d1 = math.hypot(x1 - x0, y1 - y0) or 1.0
        d2 = math.hypot(x2 - x1, y2 - y1) or 1.0
        rr = min(r, d1 / 2, d2 / 2)
        ax, ay = x1 - (x1 - x0) / d1 * rr, y1 - (y1 - y0) / d1 * rr
        bx, by = x1 + (x2 - x1) / d2 * rr, y1 + (y2 - y1) / d2 * rr
        out.append("L%.1f,%.1f Q%.1f,%.1f %.1f,%.1f" % (ax, ay, x1, y1, bx, by))
    out.append("L%.1f,%.1f" % pts[-1])
    return " ".join(out)

def wire(s, pts, color, width=3.4, optional=False):
    c = COLORS.get(color, color)
    d = smooth(pts)
    dash = ' stroke-dasharray="7 5"' if optional else ""
    s.add('<path d="%s" fill="none" stroke="#0000002a" stroke-width="%.1f" stroke-linecap="round" stroke-linejoin="round" transform="translate(1.2,1.6)"%s/>' % (d, width + 1.2, dash), "wires")
    s.add('<path d="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round" stroke-linejoin="round"%s/>' % (d, c, width, dash), "wires")
    for (x, y) in (pts[0], pts[-1]):
        s.add('<rect x="%.1f" y="%.1f" width="5" height="5" fill="#9e9e9e" stroke="%s" stroke-width="1.2"/>' % (x - 2.5, y - 2.5, c), "wires")

def manhattan(a, b, mid_y=None, mid_x=None):
    (x1, y1), (x2, y2) = a, b
    if mid_x is not None: return [a, (mid_x, y1), (mid_x, y2), b]
    my = mid_y if mid_y is not None else (y1 + y2) / 2
    return [a, (x1, my), (x2, my), b]

def leg(s, x1, y1, x2, y2):
    s.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#a0a0a0" stroke-width="2.2" stroke-linecap="round"/>' % (x1, y1, x2, y2), "top")

def resistor(s, a, b, label, bands):
    """a, b = hole positions (horizontal or vertical); bands = colour list"""
    leg(s, a[0], a[1], b[0], b[1])
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    if abs(a[0] - b[0]) >= abs(a[1] - b[1]):
        s.add('<rect x="%.1f" y="%.1f" width="26" height="9" rx="4" fill="#dcc08d" stroke="#a1887f"/>' % (mx - 13, my - 4.5), "top")
        for k, bc in enumerate(bands):
            s.add('<rect x="%.1f" y="%.1f" width="2.6" height="9" fill="%s"/>' % (mx - 8.5 + k * 4.5, my - 4.5, bc), "top")
        s.text(mx, my - 8, label, 7.5, layer="top")
    else:
        s.add('<rect x="%.1f" y="%.1f" width="9" height="26" rx="4" fill="#dcc08d" stroke="#a1887f"/>' % (mx - 4.5, my - 13), "top")
        for k, bc in enumerate(bands):
            s.add('<rect x="%.1f" y="%.1f" width="9" height="2.6" fill="%s"/>' % (mx - 4.5, my - 8.5 + k * 4.5, bc), "top")
        s.text(mx + 8, my + 3, label, 7.5, anchor="start", layer="top")

R330 = ("#ff9800", "#ff9800", "#6d4c41", "#d4af37")       # orange orange brown
R1K = ("#6d4c41", "#212121", "#d32f2f", "#d4af37")        # brown black red
R10K = ("#6d4c41", "#212121", "#ff9800", "#d4af37")       # brown black orange

def led(s, anode, cathode, color, label):
    """LED standing on two adjacent holes (anode, cathode)"""
    ax, ay = anode; kx, ky = cathode
    cx, cy = (ax + kx) / 2, min(ay, ky) - 14
    leg(s, ax, ay, ax, cy + 6); leg(s, kx, ky, kx, cy + 6)
    s.add('<path d="M%.1f,%.1f a8,8 0 0 1 16,0 v9 h-16 z" fill="%s" stroke="#00000055" opacity=".93"/>' % (cx - 8, cy - 3, color), "top")
    s.add('<rect x="%.1f" y="%.1f" width="18" height="3" rx="1" fill="%s" stroke="#00000055"/>' % (cx - 9, cy + 5, color), "top")
    s.text(cx, cy - 16, label, 7.5, layer="top")
    s.text(ax - 5, ay + 2, "+", 8, color="#d32f2f", weight="bold", anchor="end", layer="top")

def pot(s, holes, label="10k pot"):
    """3 legs in 3 adjacent holes of one row"""
    (x1, y), (x2, _), (x3, _) = holes
    for x in (x1, x2, x3): leg(s, x, y, x, y - 8)
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="26" rx="4" fill="#1565c0" stroke="#0d47a1"/>' % (x1 - 9, y - 34, x3 - x1 + 18), "top")
    s.add('<circle cx="%.1f" cy="%.1f" r="9" fill="#eeeeee" stroke="#757575"/><line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#424242" stroke-width="2.4"/>'
          % (x2, y - 21, x2, y - 21, x2 + 5, y - 27), "top")
    s.text(x2, y - 39, label, 7.5, layer="top")

def piezo(s, plus, minus, label="piezo"):
    (x1, y), (x2, _) = plus, minus
    cx = (x1 + x2) / 2
    leg(s, x1, y, x1, y - 10); leg(s, x2, y, x2, y - 10)
    s.add('<circle cx="%.1f" cy="%.1f" r="17" fill="#212121" stroke="#000"/><circle cx="%.1f" cy="%.1f" r="4" fill="#555"/>' % (cx, y - 26, cx, y - 26), "top")
    s.text(x1 - 4, y - 44, "+", 10, color="#ff5252", weight="bold", layer="top")
    s.text(cx, y - 47, label, 7.5, layer="top")

def ldr(s, a, b):
    leg(s, a[0], a[1], a[0], a[1] - 9); leg(s, b[0], b[1], b[0], b[1] - 9)
    cx = (a[0] + b[0]) / 2
    s.add('<ellipse cx="%.1f" cy="%.1f" rx="10" ry="8" fill="#ff8f00" stroke="#bf360c"/><path d="M%.1f,%.1f q3,-5 6,0 t6,0" stroke="#4e342e" fill="none" stroke-width="1.2"/>' % (cx, a[1] - 15, cx - 6, a[1] - 15), "top")
    s.text(cx, a[1] - 26, "LDR", 7.5, layer="top")

def module(s, x, y, w, h, title, sub, fill, pins, side="B"):
    """off-board module; pins = list of names along one side; returns {name: (x,y)}"""
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="5" fill="%s" stroke="#00000088" stroke-width="1.2"/>' % (x, y, w, h, fill), "top")
    ty = y + h / 2 + (6 if side == "T" else -6)
    s.text(x + w / 2, ty, title, 10.5, color="#fff", weight="bold", layer="top")
    if sub: s.text(x + w / 2, ty + 12, sub, 7.8, color="#e0e0e0", layer="top")
    out = {}
    n = len(pins)
    for i, name in enumerate(pins):
        px = x + 14 + i * (w - 28) / max(1, n - 1) if n > 1 else x + w / 2
        py = y + h if side == "B" else y
        s.add('<rect x="%.1f" y="%.1f" width="7" height="7" fill="#1b1b1b"/><rect x="%.1f" y="%.1f" width="3.5" height="3.5" fill="#ffca28"/>' % (px - 3.5, py - 3.5, px - 1.75, py - 1.75), "top")
        s.text(px, py + (-7 if side == "B" else 14), name, 7, color="#fff", weight="bold", layer="top")
        out[name] = (px, py)
    return out

def tag(s, x, y, text, color="#2e7d32"):
    w = len(text) * 5.6 + 14
    s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="16" rx="4" fill="#ffffff" stroke="%s" stroke-width="1.2"/>' % (x - w / 2, y - 11.5, w, color), "top")
    s.text(x, y + 0.5, text, 8.2, color=color, weight="bold", layer="top")

# ---------------- shared building blocks ----------------
def power_rails(s, need_3v3=True, need_5v=False, need_gnd=True):
    """J3-1 3V3 -> bottom + rail, J3-2 GND -> bottom - rail, J2-1 5V -> top + rail, J2-2 GND -> top - rail"""
    L = s.L
    out = {}
    if need_3v3:
        a = L.tap("+3V3"); r = L.rail("B+", COL0 - 1)
        wire(s, [a, (a[0], a[1] + 0.6 * PX), (r[0], a[1] + 0.6 * PX), r], "red"); out["3V3"] = "B+"
    if need_gnd:
        a = L.tap("GND", "J3"); r = L.rail("B-", COL0 + 1 + 1)
        wire(s, [a, (a[0], r[1])], "black"); out["GND"] = "B-"
    if need_5v:
        a = L.tap("+5V"); r = L.rail("T+", COL0 - 1)
        wire(s, [a, (a[0], a[1] - 0.6 * PX), (r[0], a[1] - 0.6 * PX), r], "red"); out["5V"] = "T+"
        a = L.tap("GND", "J2"); r = L.rail("T-", COL0 + 1 + 1)
        wire(s, [a, (a[0], r[1])], "black")
    return out

def jumper_same_side(s, sig1, sig2, color, row_offset, label):
    L = s.L
    a, b = L.tap(sig1), L.tap(sig2)
    h1, _ = pin_of(sig1); h2, _ = pin_of(sig2)
    if h1 == h2:
        # arch outwards past the power rails so it is obvious the jumper only
        # touches its two end holes (and does not run along other strips)
        yy = L.bb_top() - 10 if h1 == "J2" else L.bb_bottom() + 12
        wire(s, [a, (a[0], yy), (b[0], yy), b], color)
        tag(s, (a[0] + b[0]) / 2, yy + (-12 if h1 == "J2" else 24), label)
    else:
        right = L.ox + (COLS - 1) * PX + 16
        wire(s, [a, (a[0], L.bb_top() - 10), (right, L.bb_top() - 10), (right, L.bb_bottom() + 10), (b[0], L.bb_bottom() + 10), b], color)
        tag(s, (a[0] + b[0]) / 2, L.bb_top() - 22, label)

def pot_on(s, sig, col, rail_side="B", optional=False, label="10k pot"):
    """pot in row g/h? No: parts go on the free right half of the breadboard.
    The pot stands in row 'g' (bottom half) at columns col..col+2:
      left leg -> GND rail, right leg -> 3V3 rail, wiper -> the signal"""
    L = s.L
    holes = [L.hole(col + k, "h") for k in range(3)]
    pot(s, holes, label + (" (optional)" if optional else ""))
    g = L.rail("B-", col); v = L.rail("B+", col + 2)
    wire(s, [L.hole(col, "j"), (L.hole(col, "j")[0], g[1])], "black")
    wire(s, [L.hole(col + 2, "j"), (L.hole(col + 2, "j")[0], v[1])], "red")
    t = L.tap(sig)
    w = L.hole(col + 1, "j")
    h, _ = pin_of(sig)
    if h == "J2":
        wire(s, [t, (t[0], L.bb_top() - 8), (L.ox + (COLS - 1) * PX + 16, L.bb_top() - 8),
                 (L.ox + (COLS - 1) * PX + 16, L.bb_bottom() + 10), (w[0], L.bb_bottom() + 10), (w[0], w[1])], "yellow", optional=optional)
    else:
        wire(s, [t, (t[0], L.bb_bottom() + 10), (w[0], L.bb_bottom() + 10), w], "yellow", optional=optional)

# ---------------- generic legend ----------------
def legend(s, conns, notes):
    L = s.L
    n = len(conns) + len(notes)
    y = L.h - 12 - 19 * (n - 1)
    if n:
        s.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#d9dee3"/>' % (L.ox - 20, y - 16, L.w - 20, y - 16), "top")
    for i, (frm, sig, color, prefer) in enumerate(conns):
        h, p = pin_of(sig, prefer)
        c = COLORS.get(color, color)
        yy = y + i * 19
        s.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="6" stroke-linecap="round"/>' % (L.ox, yy - 4, L.ox + 22, yy - 4, c), "top")
        s.text(L.ox + 32, yy, "%s  \u2192  %s-%d  (%s)" % (frm, h, p, norm(sig)), 13, anchor="start", family="Consolas,monospace", layer="top")
    for k, t in enumerate(notes):
        s.text(L.ox, y + (len(conns) + k) * 19 + 1, t, 12.5, anchor="start", color="#5c6770", layer="top")

# ---------------- labs ----------------
def LABS():
    D = {}
    RIGHT_FREE = COL0 + 22 + 3       # first free breadboard column right of the board

    def nothing(s):
        L = s.L
        tag(s, L.ox + 470, L.bb_bottom() + 26, "Nothing to wire: the on-board USER LED (RC13) and USER button (RC12) are used", "#2e7d32")

    def usb_serial(s):
        L = s.L
        bx, by = board_xy(L, 11.89, 71.7)
        x = L.ox + (COLS - 1) * PX - 160; y = L.bb_bottom() + 52
        m = module(s, x, y, 170, 52, "USB-serial 3.3 V", "CP2102 / CH340 / FT232", "#37474f", ["GND", "TX", "RX", "3V3"], side="T")
        j5 = [board_xy(L, 11.89 + k * 2.54, 71.7) for k in range(4)]   # J5-1..4
        for k, (jp, tgt, col) in enumerate(((1, "RX", "green"), (2, "TX", "yellow"), (3, "GND", "black"))):
            sx, sy = j5[jp]
            ex, ey = m[tgt]
            mid = L.bb_bottom() + 18 + k * 8
            sx2 = sx + 28 + k * 6
            wire(s, [(sx, sy), (sx2, sy), (sx2, mid), (ex, mid), (ex, ey)], col, 2.6)

    def d_none(s): nothing(s)
    D["lab01"] = dict(title="Lab 01: nothing to wire", draw=d_none, conns=[], notes=[], bottom=48)
    D["lab02"] = dict(title="Lab 02: nothing to wire", draw=d_none, conns=[], notes=[], bottom=48)

    def d03(s): usb_serial(s)
    J5NOTE = "USB-serial adapter on J5 (end of the board): J5-2 TX \u2192 adapter RX, J5-3 RX \u2190 adapter TX, J5-4 GND. Leave J5-1 (3V3) open."
    D["lab03"] = dict(title="Lab 03: USB-serial adapter on J5", draw=d03, conns=[], notes=[J5NOTE], bottom=130)

    def d04(s, sig="RA0"):
        power_rails(s)
        pot_on(s, sig, RIGHT_FREE + 2)
    D["lab04"] = dict(title="Lab 04: potentiometer on A0 (RA0)", draw=d04,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"), ("pot wiper", "RA0", "yellow", None)],
                      notes=["pot outer legs: one to the + rail (3V3), one to the - rail (GND)"], bottom=110)

    def d05(s):
        L = s.L
        power_rails(s, need_3v3=False)
        t = L.tap("RB0")                                   # J3 side, row j
        c = RIGHT_FREE + 3
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "orange")
        resistor(s, L.hole(c, "h"), L.hole(c + 4, "h"), "330R", R330)
        led(s, L.hole(c + 4, "f"), L.hole(c + 5, "f"), "#e53935", "LED")
        wire(s, [L.hole(c + 5, "g"), (L.hole(c + 5, "g")[0] + 0, L.hole(c + 5, "g")[1]), L.hole(c + 5, "g")], "black")
        wire(s, [L.hole(c + 5, "j"), L.rail("B-", c + 5)], "black")
        power_rails(s, need_gnd=False)                    # 3V3 rail for the optional pot
        pot_on(s, "RA0", c + 10, optional=True)
    D["lab05"] = dict(title="Lab 05: LED + 330 ohm on D5 (RB0)", draw=d05,
                      conns=[("D5 to 330R", "RB0", "orange", None), ("GND to - rail", "GND", "black", "J3"),
                             ("3V3 to + rail (for the pot)", "+3V3", "red", "J3"), ("pot wiper (optional, dashed)", "RA0", "yellow", None)],
                      notes=["330R from the RB0 strip to the LED's long leg (+); short leg (-) strip to the - rail. Dashed = optional."], bottom=110)

    D["lab06"] = dict(title="Lab 06: nothing to wire", draw=d_none, conns=[], notes=[], bottom=48)

    def d07(s):
        L = s.L
        power_rails(s, need_3v3=False)
        t = L.tap("RB4"); c = RIGHT_FREE + 3
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "orange")
        piezo(s, L.hole(c, "h"), L.hole(c + 2, "h"))
        wire(s, [L.hole(c + 2, "j"), L.rail("B-", c + 2)], "black")
    D["lab07"] = dict(title="Lab 07: passive piezo on D9 (RB4)", draw=d07,
                      conns=[("piezo +", "RB4", "orange", None), ("GND to - rail", "GND", "black", "J3")], notes=[], bottom=96)

    def i2c(s, title, sub):
        L = s.L
        power_rails(s)
        # module pins ordered like the header (SDA J3-7 left of SCL J3-8) and the
        # module placed so SDA/SCL sit right under those strips: no crossings
        sda = L.tap("RB9")
        x = sda[0] - 14 - 2 * (200 - 28) / 3; y = L.bb_bottom() + 48
        m = module(s, x, y, 200, 56, title, sub, "#1a237e", ["GND", "VCC", "SDA", "SCL"], side="T")
        for k, (nm, src, col) in enumerate((("GND", ("rail", "B-"), "black"), ("VCC", ("rail", "B+"), "red"), ("SDA", ("sig", "RB9"), "blue"), ("SCL", ("sig", "RB8"), "yellow"))):
            ex, ey = m[nm]
            if src[0] == "rail":
                r = L.rail(src[1], int(round((ex - L.ox) / PX)))
                wire(s, [r, (ex, ey)], col, 2.6)
            else:
                t = L.tap(src[1])
                yy = L.bb_bottom() + 20
                wire(s, [t, (t[0], yy), (ex, yy), (ex, ey)], col, 2.6)
    def d08(s): i2c(s, "I2C module", "BME280 / MPU6050 / OLED, 3.3 V")
    I2CC = [("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"), ("SCL", "RB8", "yellow", None), ("SDA", "RB9", "blue", None)]
    D["lab08"] = dict(title="Lab 08: I2C module", draw=d08, conns=I2CC, notes=["module VCC \u2192 + rail (3V3), GND \u2192 - rail"], bottom=150)

    def d09(s): jumper_same_side(s, "RC5", "RC8", "green", 0.0, "jumper MOSI J3-5 \u2194 MISO J3-14")
    D["lab09"] = dict(title="Lab 09: SPI loopback jumper", draw=d09,
                      conns=[("jumper end (MOSI)", "RC5", "green", None), ("jumper end (MISO)", "RC8", "green", None)], notes=[], bottom=96)

    def d10(s):
        L = s.L
        d04(s)
        c = RIGHT_FREE + 8
        # LDR from + rail to node (col c+1 row h), 10k node -> - rail
        ldr(s, L.hole(c, "g"), L.hole(c + 1, "g"))
        wire(s, [L.hole(c, "j"), L.rail("B+", c)], "red")
        resistor(s, L.hole(c + 1, "i"), L.hole(c + 4, "i"), "10k", R10K)
        wire(s, [L.hole(c + 4, "j"), L.rail("B-", c + 4)], "black")
        t = L.tap("RA1")
        right = L.ox + (COLS - 1) * PX + 22
        wire(s, [t, (t[0], L.bb_top() - 16), (right, L.bb_top() - 16), (right, L.bb_bottom() + 18), (L.hole(c + 1, "j")[0], L.bb_bottom() + 18), L.hole(c + 1, "j")], "green")
        c2 = c + 7
        piezo(s, L.hole(c2, "h"), L.hole(c2 + 2, "h"), "buzzer (opt.)")
        tb = L.tap("RB4")
        wire(s, [tb, (tb[0], L.bb_bottom() + 26), (L.hole(c2, "j")[0], L.bb_bottom() + 26), L.hole(c2, "j")], "orange")
        wire(s, [L.hole(c2 + 2, "j"), L.rail("B-", c2 + 2)], "black")
    D["lab20"] = dict(title="Lab 20: pot on A0, LDR divider on A1, buzzer on D9", draw=d10,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"), ("pot wiper", "RA0", "yellow", None),
                             ("LDR / 10k junction", "RA1", "green", None), ("buzzer +", "RB4", "orange", None)],
                      notes=["LDR: + rail \u2192 junction;  10k: junction \u2192 - rail"], bottom=150)

    def top_module(s, title, sub, fill, pins, wires, rails):
        """module ABOVE the breadboard, wired to J2-side signals (row a) and the top rails"""
        L = s.L
        power_rails(s, need_3v3=False, need_5v=True, need_gnd=False) if "5V" in rails else None
        x = L.ox + (COL0 - 2) * PX; y = 12
        w = 30 + 46 * (len(pins) - 1)
        m = module(s, x, y, w, 46, title, sub, fill, pins, side="B")
        for k, (nm, src, col) in enumerate(wires):
            ex, ey = m[nm]
            yy = L.bb_top() - 8 - k * 6
            if src[0] == "rail":
                r = L.rail(src[1], int(round((ex - L.ox) / PX)))
                wire(s, [(ex, ey), (ex, r[1])], col, 2.6)
            elif src[0] == "sig":
                t = L.tap(src[1], src[2] if len(src) > 2 else None)
                wire(s, [(ex, ey), (ex, yy), (t[0], yy), t], col, 2.6)
        return m

    def d11(s):
        top_module(s, "GPS", "NEO-6M / M8N  9600 baud", "#00695c", ["VCC", "GND", "TX", "RX"],
                   [("VCC", ("rail", "T+"), "red"), ("GND", ("rail", "T-"), "black"), ("TX", ("sig", "RB10"), "green"), ("RX", ("sig", "RB11"), "yellow")], ["5V"])
    D["lab21"] = dict(title="Lab 21: GPS module on Serial2", draw=d11, top=110,
                      conns=[("5V to top + rail", "+5V", "red", "J2"), ("GND to top - rail", "GND", "black", "J2"), ("GPS TX", "RB10", "green", None), ("GPS RX (optional)", "RB11", "yellow", None)],
                      notes=["GPS TX must be 3.3 V logic; if the module is 3.3 V only, feed VCC from J3-1 instead"], bottom=96)

    def d12(s):
        L = s.L
        power_rails(s, need_3v3=False, need_5v=True, need_gnd=False)
        power_rails(s, need_3v3=False, need_5v=False)          # bottom GND rail too
        c = RIGHT_FREE + 2
        t = L.tap("RC5")
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "green")
        resistor(s, L.hole(c, "h"), L.hole(c + 4, "h"), "330R", R330)
        x = L.ox + (c + 3) * PX; y = L.bb_bottom() + 52
        m = module(s, x, y, 250, 50, "", "", "#212121", ["5V", "DIN", "GND"], side="T")
        s.text(x + 125, y + 46, "WS2812B strip (8 LEDs)", 9, color="#fff", weight="bold", layer="top")
        for k in range(8):
            s.add('<rect x="%.1f" y="%.1f" width="13" height="13" rx="2" fill="#fafafa" stroke="#999"/><circle cx="%.1f" cy="%.1f" r="4" fill="%s"/>'
                  % (x + 40 + k * 22, y + 16, x + 46.5 + k * 22, y + 22.5, ["#e53935", "#fb8c00", "#fdd835", "#43a047", "#1e88e5", "#5e35b1", "#d81b60", "#00acc1"][k]), "top")
        wire(s, [L.hole(c + 4, "j"), (L.hole(c + 4, "j")[0], L.bb_bottom() + 20), (m["DIN"][0], L.bb_bottom() + 20), m["DIN"]], "green", 2.6)
        wire(s, [L.rail("B-", c + 15), (L.rail("B-", c + 15)[0], L.bb_bottom() + 28), (m["GND"][0], L.bb_bottom() + 28), m["GND"]], "black", 2.6)
        right = L.ox + (COLS - 1) * PX + 20
        tp = L.rail("T+", COLS - 2)
        wire(s, [tp, (tp[0], L.bb_top() - 10), (right, L.bb_top() - 10), (right, L.bb_bottom() + 36), (m["5V"][0], L.bb_bottom() + 36), m["5V"]], "red", 2.6)
        tag(s, x - 60, y + 30, "1000 uF across 5V/GND", "#455a64")
    D["lab22"] = dict(title="Lab 22: WS2812B strip on MOSI (RC5)", draw=d12,
                      conns=[("RC5 to 330R \u2192 strip DIN", "RC5", "green", None), ("5V to top + rail \u2192 strip 5V", "+5V", "red", "J2"), ("GND to - rail \u2192 strip GND", "GND", "black", "J3"), ("GND to top - rail", "GND", "black", "J2")],
                      notes=["common ground is mandatory; keep BRIGHTNESS low on USB power"], bottom=120)

    def d13(s): jumper_same_side(s, "RA3", "RA4", "green", 0.0, "test 1: jumper J2-18 \u2194 J2-19")
    D["lab13"] = dict(title="Lab 13: software-serial loopback", draw=d13, top=56,
                      conns=[("soft TX", "RA3", "green", None), ("soft RX", "RA4", "green", None)], notes=[], bottom=60)

    def d14(s):
        L = s.L
        top_module(s, "RS485", "SP3485 / THVD1500 (3.3 V)", "#4e342e", ["RO", "DI", "DE+RE", "VCC", "GND"],
                   [("RO", ("sig", "RB10"), "green"), ("DI", ("sig", "RB11"), "yellow"), ("DE+RE", ("sig", "RB12"), "purple")], [])
        power_rails(s)
        m = None
        tag(s, L.ox + 470, 30, "A / B \u2192 twisted pair to the other node, 120R at both ends", "#4e342e")
        tag(s, L.ox + 470, 52, "module VCC \u2192 3V3 (J3-1), GND \u2192 GND (J2-2 or J3-2)", "#4e342e")
    D["lab24"] = dict(title="Lab 24: RS485 transceiver on Serial2", draw=d14, top=110,
                      conns=[("RO", "RB10", "green", None), ("DI", "RB11", "yellow", None), ("DE + /RE", "RB12", "purple", None), ("VCC", "+3V3", "red", "J3"), ("GND", "GND", "black", "J3")], notes=[], bottom=110)

    def d15(s):
        L = s.L
        d04(s, "RA2")
        t = L.tap("RB1")
        x = t[0]; y = L.bb_bottom() + 30
        s.add('<path d="M%.1f,%.1f l0,22 l-6,10 l12,0 z" fill="#78909c" stroke="#37474f"/>' % (x, y - 18), "top")
        wire(s, [t, (x, y - 18)], "purple", optional=True)
        s.text(x + 10, y + 12, "scope probe on D6 = 1 kHz sample clock (optional)", 11, anchor="start", color="#6a1b9a", layer="top")
        # DMA channel 2 LED: D7 = RB2 -> 330R -> LED -> GND (same build as Lab 05)
        t = L.tap("RB2")
        c = RIGHT_FREE + 9
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "orange")
        resistor(s, L.hole(c, "h"), L.hole(c + 4, "h"), "330R", R330)
        led(s, L.hole(c + 4, "f"), L.hole(c + 5, "f"), "#e53935", "LED (DMA ch2)")
        wire(s, [L.hole(c + 5, "j"), L.rail("B-", c + 5)], "black")
    D["lab14"] = dict(title="Lab 14: pot on A2 (RA2) + LED on D7 (RB2)", draw=d15,
                      conns=[("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3"), ("pot wiper", "RA2", "yellow", None),
                             ("D7 to 330R → LED", "RB2", "orange", None), ("scope probe (optional)", "RB1", "purple", None)],
                      notes=["aliasing test (step 7): remove the pot wiper wire and jumper D5 = J3-21 to J2-17 instead"], bottom=110)

    D["lab17"] = dict(title="Lab 17: nothing to wire (USB-serial on J5)", draw=d03, conns=[], notes=[J5NOTE], bottom=130)

    def d17(s):
        L = s.L
        right = L.ox + (COLS - 1) * PX
        for k, (sig, col, nm) in enumerate((("RB14", "#e53935", "U"), ("RB12", "#43a047", "V"), ("RB10", "#1e88e5", "W"))):
            t = L.tap(sig)
            c = RIGHT_FREE + 2 + k * 7
            yy = L.bb_top() - 8 - k * 6
            wire(s, [t, (t[0], yy), (L.hole(c, "a")[0], yy), L.hole(c, "a")], "orange", 2.6)
            resistor(s, L.hole(c, "c"), L.hole(c + 3, "c"), "1k", R1K)
            led(s, L.hole(c + 3, "e"), L.hole(c + 4, "e"), col, "LED " + nm)
            wire(s, [L.hole(c + 4, "a"), L.rail("T-", c + 4)], "black", 2.6)
        a = L.tap("GND", "J2"); r = L.rail("T-", COL0 + 2)
        wire(s, [a, (a[0], r[1])], "black")
        for sig in ("RB15", "RB13", "RB11"):
            t = L.tap(sig)
            s.add('<path d="M%.1f,%.1f l-5,-16 l10,0 z" fill="#78909c" stroke="#37474f"/>' % (t[0], t[1] - 3), "top")
        tag(s, L.ox + 640, L.bb_bottom() + 26, "grey probes on PWM1L / 2L / 3L (J2-9, J2-7, J2-5): scope shows complementary PWM + dead time", "#6a1b9a")
    D["lab16"] = dict(title="Lab 16: three LEDs on PWM1H / PWM2H / PWM3H", draw=d17, top=70,
                      conns=[("1k \u2192 LED U", "RB14", "orange", None), ("1k \u2192 LED V", "RB12", "orange", None), ("1k \u2192 LED W", "RB10", "orange", None), ("GND to top - rail", "GND", "black", "J2"),
                             ("scope probe (PWM1L)", "RB15", "grey", None), ("scope probe (PWM2L)", "RB13", "grey", None), ("scope probe (PWM3L)", "RB11", "grey", None)],
                      notes=["to SEE the sine: J2-8 %s 10k %s node %s 100nF %s GND, node %s A0 (J2-15)" % ("→", "→", "→", "→", "→")], bottom=96)

    def d18(s): jumper_same_side(s, "RC0", "RC1", "green", 0.0, "jumper U3TX J2-14 \u2194 U3RX J2-20")
    D["lab12"] = dict(title="Lab 12: UART3 loopback jumper", draw=d18, top=56,
                      conns=[("U3TX", "RC0", "green", None), ("U3RX", "RC1", "green", None)], notes=[], bottom=60)

    D["lab19"] = dict(title="Lab 19: nothing to wire (USB-serial on J5)", draw=d03, conns=[], notes=[J5NOTE], bottom=130)

    def d20(s):
        i2c(s, "SSD1306 OLED", "0.96in 128x64, I2C 0x3C")
        pot_on(s, "RA0", RIGHT_FREE + 14)
    D["lab23"] = dict(title="Lab 23: SSD1306 OLED on I2C (+ optional pot)", draw=d20,
                      conns=I2CC + [("pot wiper (optional)", "RA0", "yellow", None)], notes=[], bottom=150)

    def d21(s):
        usb_serial(s)
        L = s.L
        tag(s, L.ox + 180, L.bb_bottom() + 70, "optional: USB power meter in the USB-C cable", "#6a1b9a")
    D["lab18"] = dict(title="Lab 18: USB-serial on J5, power meter optional", draw=d21, conns=[], notes=[J5NOTE], bottom=130)

    def d22(s):
        jumper_same_side(s, "RA3", "RA2", "green", 0.0, "jumper DACOUT1 J2-18 \u2192 A2 J2-17")
        power_rails(s)
        pot_on(s, "RA0", RIGHT_FREE + 2)
    D["lab15"] = dict(title="Lab 15: DAC read-back jumper + pot on the comparator", draw=d22, top=56,
                      conns=[("jumper (DACOUT1)", "RA3", "green", None), ("jumper (A2)", "RA2", "green", None), ("3V3 to + rail", "+3V3", "red", "J3"),
                             ("GND to - rail", "GND", "black", "J3"), ("pot wiper (CMP1A)", "RA0", "yellow", None)], notes=[], bottom=110)

    def bottom_module(s, title, sub, fill, pins, wires, x_col=None, w=None):
        L = s.L
        x = L.ox + (x_col if x_col is not None else COL0 + 6) * PX; y = L.bb_bottom() + 56
        w = w or (30 + 40 * (len(pins) - 1))
        m = module(s, x, y, w, 54, title, sub, fill, pins, side="T")
        # crossing-free levels: a wire that travels LEFT turns high, one that
        # travels RIGHT turns low (sorted by horizontal travel).
        sig_w = [(k, nm, src) for k, (nm, src, col) in enumerate(wires) if src[0] == "sig" and pin_of(src[1])[0] == "J3"]
        travel = sorted(sig_w, key=lambda t: m[t[1]][0] - L.tap(t[2][1])[0])
        level = {k: i for i, (k, _, _) in enumerate(travel)}
        span = (y - 8) - (L.bb_bottom() + 12)
        step = span / max(1, len(travel))
        for k, (nm, src, col) in enumerate(wires):
            ex, ey = m[nm]
            if src[0] == "rail":
                r = L.rail(src[1], int(round((ex - L.ox) / PX)) - 1)
                wire(s, [r, (r[0], ey - 10 - k * 3), (ex, ey - 10 - k * 3), (ex, ey)], col, 2.6)
            else:
                t = L.tap(src[1], src[2] if len(src) > 2 else None)
                yy = L.bb_bottom() + 12 + step * (level.get(k, k) + 0.5)
                if pin_of(src[1])[0] == "J2":
                    right = L.ox + (COLS - 1) * PX + 16 + k * 5
                    wire(s, [t, (t[0], L.bb_top() - 8 - k * 5), (right, L.bb_top() - 8 - k * 5), (right, yy), (ex, yy), (ex, ey)], col, 2.6)
                else:
                    wire(s, [t, (t[0], yy), (ex, yy), (ex, ey)], col, 2.6)
        return m

    def d23(s):
        power_rails(s, need_3v3=True, need_5v=True)
        m = bottom_module(s, "CAN transceiver", "MCP2562 / SN65HVD230", "#4e342e", ["TXD", "RXD", "VDD", "VIO", "GND"],
                          [("TXD", ("sig", "RC6"), "green"), ("RXD", ("sig", "RC7"), "yellow"), ("VIO", ("rail", "B+"), "red"), ("GND", ("rail", "B-"), "black")],
                          x_col=RIGHT_FREE - 6, w=230)
        L = s.L
        tp = L.rail("T+", COLS - 3); vx, vy = m["VDD"]
        right = L.ox + (COLS - 1) * PX + 34
        wire(s, [tp, (tp[0], L.bb_top() - 30), (right, L.bb_top() - 30), (right, vy - 26), (vx, vy - 26), (vx, vy)], "red", 2.6)
        tag(s, L.ox + (RIGHT_FREE + 12) * PX + 120, L.bb_bottom() + 80, "CANH / CANL \u2192 other node, 120R at both ends", "#4e342e")
    D["lab26"] = dict(title="Lab 26: CAN transceiver on RC6 / RC7", draw=d23, top=60,
                      conns=[("CAN TXD", "RC6", "green", None), ("CAN RXD", "RC7", "yellow", None), ("5V to top + rail \u2192 VDD", "+5V", "red", "J2"),
                             ("3V3 to + rail \u2192 VIO (MCP2562)", "+3V3", "red", "J3"), ("GND to bottom - rail", "GND", "black", "J3"), ("GND to top - rail", "GND", "black", "J2")], notes=[], bottom=150)

    def d24(s): jumper_same_side(s, "RB11", "RB10", "green", 0.0, "test 1: jumper TX J2-5 \u2192 RX J2-4  (test 2: LIN transceiver TXD / RXD)")
    D["lab25"] = dict(title="Lab 25: LIN loopback (or transceiver)", draw=d24, top=56,
                      conns=[("TX \u2192 transceiver TXD", "RB11", "green", None), ("RX \u2190 transceiver RXD", "RB10", "green", None)], notes=[], bottom=60)

    def d25(s):
        power_rails(s)
        bottom_module(s, "Two-Wire ETH Click", "LAN8651  10BASE-T1S MAC-PHY", "#283593",
                      ["SDI", "SCK", "IRQ", "RST", "CS", "SDO", "3V3", "GND"],
                      [("SDI", ("sig", "RC5"), "green"), ("SCK", ("sig", "RC4"), "yellow"), ("IRQ", ("sig", "RB7"), "orange"), ("RST", ("sig", "RD8"), "pink"),
                       ("CS", ("sig", "RC9"), "purple"), ("SDO", ("sig", "RC8"), "blue"), ("3V3", ("rail", "B+"), "red"), ("GND", ("rail", "B-"), "black")],
                      x_col=COL0 + 1, w=430)
        L = s.L
        tag(s, L.ox + (COL0 + 1) * PX + 215, L.bb_bottom() + 118, "T1S pair N / P \u2192 other node, 100R termination at both ends", "#283593")
    D["lab27"] = dict(title="Lab 27: LAN8651 10BASE-T1S click on SPI", draw=d25,
                      conns=[("SCK", "RC4", "yellow", None), ("SDI (MOSI)", "RC5", "green", None), ("SDO (MISO)", "RC8", "blue", None), ("CS", "RC9", "purple", None),
                             ("IRQ", "RB7", "orange", None), ("RST", "RD8", "pink", None), ("3V3 to + rail", "+3V3", "red", "J3"), ("GND to - rail", "GND", "black", "J3")],
                      notes=[], bottom=200)
    # ---------------- (old 26) advanced timers ----------------
    def d26(s):
        L = s.L
        power_rails(s, need_3v3=False)
        # D9 = RB4 (J3-15) -> piezo, D10 = RB5 (J3-11) -> scope, D11 = RB6 (J3-10) -> 1k + LED
        c = RIGHT_FREE + 2
        t = L.tap("RB4")
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "orange", optional=True)
        piezo(s, L.hole(c, "h"), L.hole(c + 2, "h"), "piezo (opt.)")
        wire(s, [L.hole(c + 2, "j"), L.rail("B-", c + 2)], "black")
        c2 = c + 7
        t = L.tap("RB6")
        wire(s, [t, (t[0], L.bb_bottom() + 18), (L.hole(c2, "j")[0], L.bb_bottom() + 18), L.hole(c2, "j")], "green")
        resistor(s, L.hole(c2, "h"), L.hole(c2 + 3, "h"), "1k", R1K)
        led(s, L.hole(c2 + 3, "f"), L.hole(c2 + 4, "f"), "#43a047", "LED 5 Hz")
        wire(s, [L.hole(c2 + 4, "j"), L.rail("B-", c2 + 4)], "black")
        t = L.tap("RB5")
        s.add('<path d="M%.1f,%.1f l-5,16 l10,0 z" fill="#78909c" stroke="#37474f"/>' % (t[0], t[1] + 3), "top")
        tag(s, L.ox + 300, L.bb_bottom() + 40, "grey probe: Timer2 on D10 (J3-11), 50 Hz", "#6a1b9a")
    D["lab11"] = dict(title="Lab 11: timers - LED on D11, piezo on D9, probe on D10", draw=d26,
                      conns=[("Timer1 (every 1 ms) → piezo + (optional)", "RB4", "orange", None), ("Timer3 (every 100 ms) → 1k → LED +", "RB6", "green", None),
                             ("Timer2 (every 10 ms, scope probe)", "RB5", "grey", None), ("GND to - rail", "GND", "black", "J3")],
                      notes=["no parts needed: the Serial Monitor shows how often each timer fires"], bottom=110)

    # ---------------- 28 watchdog / software reset / clock switch ----------------
    def d28(s):
        usb_serial(s)
        L = s.L
        power_rails(s, need_3v3=False)
        # D9 = RB4 (J3-15) = reference clock output FOSC/2000 -> 1k + LED (optional) and scope probe
        c = RIGHT_FREE + 9          # right of the USB-serial wires
        t = L.tap("RB4")
        wire(s, [t, (t[0], L.bb_bottom() + 10), (L.hole(c, "j")[0], L.bb_bottom() + 10), L.hole(c, "j")], "orange", optional=True)
        resistor(s, L.hole(c, "h"), L.hole(c + 3, "h"), "1k", R1K)
        led(s, L.hole(c + 3, "f"), L.hole(c + 4, "f"), "#43a047", "LED: 16 Hz on LPRC")
        wire(s, [L.hole(c + 4, "j"), L.rail("B-", c + 4)], "black")
        s.add('<path d="M%.1f,%.1f l-5,16 l10,0 z" fill="#78909c" stroke="#37474f"/>' % (t[0], t[1] + 3), "top")
        tag(s, L.ox + 300, L.bb_bottom() + 40, "grey probe: clock output on D9 (J3-15) = FOSC / 2000", "#6a1b9a")
    D["lab28"] = dict(title="Lab 28: USB-serial on J5, clock output on D9 (optional LED / scope)", draw=d28,
                      conns=[("REFCLKO (FOSC / 2000) → 1k → LED + (optional)", "RB4", "orange", None), ("GND to - rail", "GND", "black", "J3")],
                      notes=[J5NOTE, "100 kHz at 100 MIPS, 4 kHz at 4 MIPS, 2 kHz at 2 MIPS, 16 Hz on LPRC"], bottom=150)

    # ---------------- 29 line follower robot: HC-05 + IR array + 2 servos ----------------
    def pinbox(s, px, py, name, below):
        s.add('<rect x="%.1f" y="%.1f" width="7" height="7" fill="#1b1b1b"/><rect x="%.1f" y="%.1f" width="3.5" height="3.5" fill="#ffca28"/>' % (px - 3.5, py - 3.5, px - 1.75, py - 1.75), "top")
        s.text(px, py + (14 if below else -7), name, 7, color="#fff", weight="bold", layer="top")

    def d29(s):
        L = s.L
        power_rails(s, need_3v3=False, need_5v=True, need_gnd=True)
        # --- top: HC-05 (pins drawn mirrored so the wires do not cross) ---
        x = L.ox + (COL0 - 2) * PX; y = 12
        hc = module(s, x, y, 30 + 46 * 4, 46, "HC-05", "Bluetooth SPP, 9600", "#0d47a1", ["VCC", "GND", "TXD", "RXD", "STATE"], side="B")
        # a wire travelling LEFT turns higher the further left its pin is
        for k, (nm, sig, col) in enumerate((("STATE", "RB12", "purple"), ("RXD", "RB11", "yellow"), ("TXD", "RB10", "green"))):
            ex, ey = hc[nm]; t = L.tap(sig); yy = L.bb_top() - 8 - k * 7
            wire(s, [(ex, ey), (ex, yy), (t[0], yy), t], col, 2.6)
        for nm, rail, col in (("VCC", "T+", "red"), ("GND", "T-", "black")):
            ex, ey = hc[nm]; r = L.rail(rail, int(round((ex - L.ox) / PX)))
            wire(s, [(ex, ey), (ex, r[1])], col, 2.6)
        # --- top: 5-channel IR array on RA0..RA4 (J2-15..19) ---
        x2 = L.ox + (COL0 + 12) * PX
        ir = module(s, x2, y, 30 + 46 * 6, 46, "IR line sensor x5", "TCRT5000, analog OUT, VCC 3V3", "#1b5e20",
                    ["OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "GND", "VCC"], side="B")
        for k, (nm, sig, col) in enumerate((("OUT5", "RA4", "cyan"), ("OUT4", "RA3", "blue"), ("OUT3", "RA2", "green"),
                                            ("OUT2", "RA1", "orange"), ("OUT1", "RA0", "pink"))):
            ex, ey = ir[nm]; t = L.tap(sig); yy = L.bb_top() - 8 - k * 6
            wire(s, [(ex, ey), (ex, yy), (t[0], yy), t], col, 2.6)
        ex, ey = ir["GND"]; r = L.rail("T-", int(round((ex - L.ox) / PX)))
        wire(s, [(ex, ey), (ex, r[1])], "black", 2.6)
        ex, ey = ir["VCC"]
        wire(s, [(ex, ey), (ex, ey + 12)], "red", 2.6, optional=True)
        tag(s, ex + 46, ey + 22, "VCC → 3V3 J3-1", "#d32f2f")
        # --- bottom: servos + battery. SIG on top of each servo, + / - underneath ---
        ym = L.bb_bottom() + 56; hm = 54
        tR, tL = L.tap("RB1"), L.tap("RB0")                  # J3-20, J3-21
        servos = []
        # the "-" pin sits where no signal wire passes above it: left end of the
        # right servo, right end of the left servo
        for (x0, title, sig_x, mx) in ((tR[0] - 49, "RIGHT servo", tR[0], 14), (L.ox + (COL0 + 35) * PX, "LEFT servo", None, 90)):
            module(s, x0, ym, 110, hm, title, "continuous 360°", "#37474f", [], side="T")
            sx = sig_x if sig_x is not None else x0 + 22
            pinbox(s, sx, ym, "SIG", True)
            pinbox(s, x0 + mx, ym, "−", True)               # brown / black
            pinbox(s, x0 + 70, ym + hm, "+", False)              # red, underneath
            servos.append((x0, sx, mx))
        # right servo: straight down from RB1;  left servo: RB0 turns above it
        wire(s, [tR, (tR[0], ym)], "orange", 2.6)
        yyL = L.bb_bottom() + 22
        wire(s, [tL, (tL[0], yyL), (servos[1][1], yyL), (servos[1][1], ym)], "orange", 2.6)
        for (x0, _, mx) in servos:                               # servo - -> bottom GND rail
            r = L.rail("B-", int(round((x0 + mx - L.ox) / PX)))
            wire(s, [(x0 + mx, ym), (x0 + mx, r[1])], "brown", 2.6)
        xb = L.ox + (COL0 + 44) * PX
        module(s, xb, ym, 120, hm, "4 x AA", "4.8 - 6 V", "#4e342e", [], side="T")
        pinbox(s, xb + 90, ym, "−", True)
        pinbox(s, xb + 30, ym + hm, "+", False)
        r = L.rail("B-", int(round((xb + 90 - L.ox) / PX)))
        wire(s, [(xb + 90, ym), (xb + 90, r[1])], "black", 2.6)
        yb = ym + hm + 14                                         # red bus under everything
        wire(s, [(servos[0][0] + 70, ym + hm), (servos[0][0] + 70, yb), (xb + 30, yb), (xb + 30, ym + hm)], "red", 2.8)
        wire(s, [(servos[1][0] + 70, ym + hm), (servos[1][0] + 70, yb)], "red", 2.8)
        right = L.ox + (COLS - 1) * PX + 16
        tp = L.rail("T+", COLS - 1)
        wire(s, [(xb + 30, yb), (right, yb), (right, tp[1]), tp], "red", 2.8)
        tag(s, xb - 60, yb + 18, "battery + → servos + and top + rail (J2-1)", "#d32f2f")
    D["lab29"] = dict(title="Lab 29: line follower - HC-05 on Serial2, IR array on A0-A4, servos on D5 / D6", draw=d29, top=110,
                      conns=[("HC-05 TXD → Serial2 RX", "RB10", "green", None), ("HC-05 RXD ← Serial2 TX", "RB11", "yellow", None),
                             ("HC-05 STATE (optional)", "RB12", "purple", None), ("IR OUT1 (leftmost)", "RA0", "pink", None),
                             ("IR OUT2", "RA1", "orange", None), ("IR OUT3 (middle)", "RA2", "green", None), ("IR OUT4", "RA3", "blue", None),
                             ("IR OUT5 (rightmost)", "RA4", "cyan", None), ("RIGHT servo signal", "RB1", "orange", None),
                             ("LEFT servo signal", "RB0", "orange", None), ("IR array VCC (3.3 V only!)", "+3V3", "red", "J3"),
                             ("5V pin ← battery + (and HC-05 VCC)", "+5V", "red", "J2"), ("GND", "GND", "black", "J3")],
                      notes=["servo brown/black → GND rail, red → battery +. All grounds together. Optional piezo on D9 (J3-15).",
                             "USB-serial on J5 for the Serial Monitor (115200) is optional once the phone works."], bottom=170)

    # ---------------- 30 PID temperature: MCP9700A on A0, MOSFET heater on D5 ----------------
    def d30(s):
        L = s.L
        power_rails(s, need_3v3=False, need_5v=True, need_gnd=True)
        # top: sensor, VOUT straight above RA0 (J2-15)
        t = L.tap("RA0")
        w = 30 + 46 * 2
        x = t[0] - (14 + (w - 28) / 2); y = 12
        m = module(s, x, y, w, 46, "MCP9700A", "or TMP36, taped on the resistor", "#00695c", ["VDD", "VOUT", "GND"], side="B")
        wire(s, [m["VOUT"], t], "yellow", 2.6)
        r = L.rail("T-", int(round((m["GND"][0] - L.ox) / PX)))
        wire(s, [m["GND"], (m["GND"][0], r[1])], "black", 2.6)
        wire(s, [m["VDD"], (m["VDD"][0], m["VDD"][1] + 12)], "red", 2.6, optional=True)
        tag(s, m["VDD"][0] - 50, m["VDD"][1] + 22, "VDD → 3V3 J3-1", "#d32f2f")
        # bottom: heater driver. GATE straight below RB0 (J3-21), GND up to the rail, 5V underneath
        tg = L.tap("RB0")
        ym = L.bb_bottom() + 56; hm = 70; x0 = tg[0] - 22; wm = 170
        module(s, x0, ym, wm, hm, "heater driver", "N-MOSFET + 10R 5W (100R gate, 10k to GND)", "#bf360c", [], side="T")
        pinbox(s, tg[0], ym, "GATE", True)
        pinbox(s, x0 + wm - 20, ym, "GND", True)
        pinbox(s, x0 + wm / 2, ym + hm, "5V", False)
        wire(s, [tg, (tg[0], ym)], "orange", 2.6)
        r = L.rail("B-", int(round((x0 + wm - 20 - L.ox) / PX)))
        wire(s, [(x0 + wm - 20, ym), (x0 + wm - 20, r[1])], "black", 2.6)
        right = L.ox + (COLS - 1) * PX + 16
        tp = L.rail("T+", COLS - 1)
        yb = ym + hm + 14
        wire(s, [(x0 + wm / 2, ym + hm), (x0 + wm / 2, yb), (right, yb), (right, tp[1]), tp], "red", 2.8)
        tag(s, x0 + wm / 2 + 190, yb + 16, "5V (J2-1, via the top + rail) → 10R → drain", "#d32f2f")
    D["lab30"] = dict(title="Lab 30: PID temperature - MCP9700A on A0, MOSFET heater on D5", draw=d30, top=110,
                      conns=[("sensor VOUT", "RA0", "yellow", None), ("sensor VDD (3.3 V)", "+3V3", "red", "J3"),
                             ("MOSFET gate (via 100R)", "RB0", "orange", None), ("heater resistor ← 5V", "+5V", "red", "J2"),
                             ("GND", "GND", "black", "J3")],
                      notes=["heater: 5V → 10R 5W → MOSFET drain, source → GND, 10k gate → GND. It gets HOT: don't touch.",
                             "no parts? send x in the Serial Monitor: the sketch has a simulated heater."], bottom=170)

    # ---------------- (old 27) pin change ----------------
    def d27(s):
        L = s.L
        power_rails(s, need_3v3=False, need_5v=False)
        # top half: button 2 on RC0 (J2-14), encoder A/B on RC1/RC2 (J2-20/21), INT1 button on RB10 (J2-4)
        a = L.tap("GND", "J2"); r = L.rail("T-", COL0 + 2)
        wire(s, [a, (a[0], r[1])], "black")
        def topbutton(sig, col, label, color):
            t = L.tap(sig)
            yy = L.bb_top() - 8 - (col - RIGHT_FREE) * 0.4
            wire(s, [t, (t[0], yy), (L.hole(col, "a")[0], yy), L.hole(col, "a")], color, 2.6)
            x1, y1 = L.hole(col, "c"); x2, _ = L.hole(col + 2, "c")
            for x in (x1, x2): leg(s, x, y1, x, y1 - 7)
            s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="16" rx="3" fill="#eceff1" stroke="#455a64"/><circle cx="%.1f" cy="%.1f" r="5" fill="#546e7a"/>'
                  % (x1 - 6, y1 - 23, x2 - x1 + 12, (x1 + x2) / 2, y1 - 15), "top")
            s.text((x1 + x2) / 2, y1 - 28, label, 9, layer="top")
            wire(s, [L.hole(col + 2, "a"), L.rail("T-", col + 2)], "black", 2.6)
        topbutton("RB10", RIGHT_FREE + 1, "INT1 button", "purple")
        topbutton("RC0", RIGHT_FREE + 6, "button 2", "green")
        # encoder: A, C, B legs in row c
        ce = RIGHT_FREE + 12
        xa, ya = L.hole(ce, "c"); xc, _ = L.hole(ce + 1, "c"); xb, _ = L.hole(ce + 2, "c")
        for x in (xa, xc, xb): leg(s, x, ya, x, ya - 7)
        s.add('<rect x="%.1f" y="%.1f" width="%.1f" height="22" rx="3" fill="#37474f"/><circle cx="%.1f" cy="%.1f" r="8" fill="#b0bec5" stroke="#263238"/>'
              % (xa - 8, ya - 29, xb - xa + 16, xc, ya - 18), "top")
        s.text(xc + 16, ya - 34, "rotary encoder (A C B)", 9, layer="top")
        for sig, x, col in (("RC1", xa, "yellow"), ("RC2", xb, "blue")):
            t = L.tap(sig)
            yy = L.bb_top() - 16 if sig == "RC1" else L.bb_top() - 22
            wire(s, [t, (t[0], yy), (x, yy), L.hole(int(round((x - L.ox) / PX)), "a")], col, 2.6)
        wire(s, [L.hole(ce + 1, "a"), L.rail("T-", ce + 1)], "black", 2.6)
        # test-signal jumper on the J3 side: D9 = RB4 (J3-15) -> RC3 (J3-22)
        jumper_same_side(s, "RB4", "RC3", "orange", 0.0, "jumper D9 J3-15 → RC3 J3-22 (send 't 1000')")
    D["lab10"] = dict(title="Lab 10: pin-change interrupts - buttons, encoder, INT1, test signal", draw=d27, top=70,
                      conns=[("button 2 (other leg → GND)", "RC0", "green", None), ("encoder A", "RC1", "yellow", None), ("encoder B", "RC2", "blue", None),
                             ("INT1 button (other leg → GND)", "RB10", "purple", None), ("GND to top - rail", "GND", "black", "J2"),
                             ("test signal jumper from", "RB4", "orange", None), ("test signal jumper to", "RC3", "orange", None)],
                      notes=["encoder middle pin C → GND; all inputs use the internal pull-ups"], bottom=70)
    return D

def content_bottom(s):
    """lowest y used by parts and wires (so the legend can sit right under it)"""
    ys = []
    for item in s.parts + s.wires + s.top:
        for m in re.finditer(r'y="(-?[\d.]+)"|cy="(-?[\d.]+)"|y[12]="(-?[\d.]+)"', item):
            v = next(g for g in m.groups() if g is not None)
            ys.append(float(v))
        for m in re.finditer(r'[ML]\s?(-?[\d.]+),(-?[\d.]+)', item):
            ys.append(float(m.group(2)))
        for hm in re.finditer(r'y="(-?[\d.]+)"\s+width="[\d.]+"\s+height="([\d.]+)"', item):
            ys.append(float(hm.group(1)) + float(hm.group(2)))
    return max(ys) if ys else 0

def build(spec):
    n_legend = len(spec["conns"]) + len(spec["notes"])
    L = Layout(spec.get("top", 40), spec.get("bottom", 80) + 16 * n_legend + 8)
    s = Svg(L)
    draw_breadboard(s)
    draw_nano(s)
    spec["draw"](s)
    # shrink the canvas: legend directly under the lowest drawn element
    L.h = content_bottom(s) + 30 + 19 * n_legend
    legend(s, spec["conns"], spec["notes"])
    return s.render(spec["title"])

def main():
    os.makedirs(OUT, exist_ok=True)
    res = {}
    for key, spec in LABS().items():
        svg = build(spec)
        res[key] = svg
        with open(os.path.join(OUT, key + ".svg"), "w", encoding="utf-8") as f:
            f.write(svg)
    with open(os.path.join(OUT, "wiring.json"), "w", encoding="utf-8") as f:
        json.dump(res, f)
    print("wrote", len(res), "diagrams to", OUT)

if __name__ == "__main__":
    main()
