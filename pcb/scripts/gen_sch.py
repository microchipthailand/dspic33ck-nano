"""Generate KiCad schematic + symbol library for the dsPIC33CK Nano board from design.py.
Run with KiCad's bundled python:  python.exe -I gen_sch.py <kicad_share_dir> <out_dir>
"""
import os, re, sys, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS, DSPIC_PINS

SHARE, OUT = sys.argv[1], sys.argv[2]
PROJECT = "dspic33ck_nano"
ROOT_UUID = str(uuid.uuid5(uuid.NAMESPACE_DNS, PROJECT + "-root"))
U = lambda *k: str(uuid.uuid5(uuid.NAMESPACE_DNS, PROJECT + "/" + "/".join(map(str, k))))


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------- custom symbols (lib "Board") ----------
def pin_sexpr(etype, x, y, rot, length, name, num):
    return (f'(pin {etype} line (at {x} {y} {rot}) (length {length}) '
            f'(name {q(name)} (effects (font (size 1.27 1.27)))) '
            f'(number {q(num)} (effects (font (size 1.27 1.27)))))')


def make_symbol(name, ref, value, fp, body, pins, desc):
    props = (f'(property "Reference" {q(ref)} (at 0 {body[1] + 2.54} 0) (effects (font (size 1.27 1.27))))'
             f'(property "Value" {q(value)} (at 0 {body[3] - 2.54} 0) (effects (font (size 1.27 1.27))))'
             f'(property "Footprint" {q(fp)} (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))'
             f'(property "Datasheet" "~" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))'
             f'(property "Description" {q(desc)} (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))')
    x1, y1, x2, y2 = body
    rect = f'(rectangle (start {x1} {y1}) (end {x2} {y2}) (stroke (width 0.254) (type default)) (fill (type background)))'
    return (f'(symbol {q(name)} (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes) {props}'
            f'(symbol {q(name + "_0_1")} {rect}) (symbol {q(name + "_1_1")} {" ".join(pins)}))')


def dspic_symbol():
    pins = []
    for p in range(1, 49):
        nm = DSPIC_PINS[p]
        if nm in ("VDD", "AVDD"):
            et = "power_in"
        elif nm in ("VSS", "AVSS"):
            et = "power_in"
        elif nm == "~{MCLR}":
            et = "input"
        else:
            et = "bidirectional"
        if p <= 24:   # left column, top->bottom
            pins.append(pin_sexpr(et, -30.48, 30.48 - (p - 1) * 2.54, 0, 5.08, nm, p))
        else:         # right column, bottom->top so the package order reads naturally
            pins.append(pin_sexpr(et, 30.48, 30.48 - (48 - p) * 2.54, 180, 5.08, nm, p))
    return make_symbol("dsPIC33CK256MC005-I_PT", "U", "dsPIC33CK256MC005-I/PT", "Package_QFP:TQFP-48_7x7mm_P0.5mm",
                       (-25.4, 33.02, 25.4, -30.48), pins, "dsPIC33CK256MC005, 256KB flash, TQFP-48 7x7")


def ldo_symbol():
    pins = [pin_sexpr("power_in", -10.16, 0, 0, 2.54, "VIN", 2),
            pin_sexpr("power_out", 10.16, 0, 180, 2.54, "VOUT", 3),
            pin_sexpr("power_in", 0, -7.62, 90, 2.54, "GND", 1)]
    return make_symbol("MCP1703A-3302_SOT89", "U", "MCP1703A-3302E/MB", "Package_TO_SOT_SMD:SOT-89-3",
                       (-7.62, 2.54, 7.62, -5.08), pins, "250mA 16V LDO 3.3V, SOT-89: 1=GND 2/tab=VIN 3=VOUT")


CUSTOM = {"Board:dsPIC33CK256MC005-I_PT": dspic_symbol(), "Board:MCP1703A-3302_SOT89": ldo_symbol()}


# ---------- library symbol extraction ----------
def extract(lib, name):
    t = open(os.path.join(SHARE, "symbols", lib + ".kicad_sym"), encoding="utf-8").read()
    i = t.index(f'(symbol "{name}"')
    d = 0
    for j in range(i, len(t)):
        if t[j] == "(":
            d += 1
        elif t[j] == ")":
            d -= 1
            if d == 0:
                break
    s = t[i:j + 1]
    assert "(extends" not in s, name
    return s


def lib_symbol(lib_id):
    if lib_id in CUSTOM:
        s = CUSTOM[lib_id]
        name = lib_id.split(":")[1]
    else:
        lib, name = lib_id.split(":")
        s = extract(lib, name)
    return s.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)


def pins_of(sym_text):
    out = {}
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)[\s\S]*?\(number "([^"]*)"', sym_text):
        out.setdefault(m.group(4), []).append((float(m.group(1)), float(m.group(2)), int(m.group(3))))
    return out


# ---------- schematic ----------
items, lib_syms = [], {}
point_net = {}
def claim(pt, net, who):
    prev = point_net.setdefault(pt, (net, who))
    if prev[0] != net:
        raise SystemExit(f"NET COLLISION at {pt}: {prev} vs {(net, who)}")

STUB = 2.54
LBL_ANGLE = {0: 180, 180: 0, 90: 270, 270: 90}
DIR = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}  # sheet direction pointing away from body

for ref, lib_id, value, fp, nets, (sx, sy), mpn in PARTS:
    sx, sy = round(round(sx / 2.54) * 2.54, 2), round(round(sy / 2.54) * 2.54, 2)
    if lib_id not in lib_syms:
        lib_syms[lib_id] = lib_symbol(lib_id)
    pinmap = pins_of(lib_syms[lib_id])
    is_pwr = ref.startswith("#")
    props = [f'(property "Reference" {q(ref)} (at {sx} {sy - 6} 0) {"(hide yes) " if is_pwr else ""}(effects (font (size 1.27 1.27))))',
             f'(property "Value" {q(value)} (at {sx} {sy + 6} 0) {"(hide yes) " if is_pwr else ""}(effects (font (size 1.27 1.27))))',
             f'(property "Footprint" {q(fp)} (at {sx} {sy} 0) (hide yes) (effects (font (size 1.27 1.27))))',
             f'(property "Datasheet" "~" (at {sx} {sy} 0) (hide yes) (effects (font (size 1.27 1.27))))']
    if mpn:
        props.append(f'(property "MPN" {q(mpn)} (at {sx} {sy} 0) (hide yes) (effects (font (size 1.27 1.27))))')
    pin_uuids = []
    drawn = set()
    for num, net in nets.items():
        pin_uuids.append(f'(pin {q(num)} (uuid {U(ref, "pin", num)}))')
        for k, (px, py, rot) in enumerate(pinmap[num]):
            ax, ay = round(sx + px, 2), round(sy - py, 2)
            if (ax, ay) in drawn:       # stacked pins (e.g. USB VBUS A9/B9) share one endpoint
                continue
            drawn.add((ax, ay))
            claim((ax, ay), net, ref + "." + num)
            if net is None:
                items.append(f'(no_connect (at {ax} {ay}) (uuid {U(ref, "nc", num, k)}))')
                continue
            dx, dy = DIR[rot]
            bx, by = round(ax + dx * STUB, 2), round(ay + dy * STUB, 2)
            claim((bx, by), net, ref + "." + num)
            items.append(f'(wire (pts (xy {ax} {ay}) (xy {bx} {by})) (stroke (width 0) (type default)) (uuid {U(ref, "w", num, k)}))')
            ang = LBL_ANGLE[rot]
            just = "right" if ang in (180, 270) else "left"
            items.append(f'(label {q(net)} (at {bx} {by} {ang}) (effects (font (size 1.27 1.27)) (justify {just} bottom)) (uuid {U(ref, "l", num, k)}))')
    items.append(
        f'(symbol (lib_id {q(lib_id)}) (at {sx} {sy} 0) (unit 1) (exclude_from_sim no) (in_bom {"no" if is_pwr else "yes"}) '
        f'(on_board {"no" if is_pwr else "yes"}) (dnp no) (uuid {U(ref)}) {" ".join(props)} {" ".join(pin_uuids)} '
        f'(instances (project {q(PROJECT)} (path {q("/" + ROOT_UUID)} (reference {q(ref)}) (unit 1)))))')

notes = [
    "dsPIC33CK256MC005-I/PT Nano-style board - USB-C 5V power only (no USB data)",
    "U2 MCP1703A: 1uF in / 1u+10uF out (stable 1-22uF ceramic). Header 5V pin is after D1, so external 5V cannot back-feed USB.",
    "Each VDD pin gets 100nF placed at the pin. AVDD fed through FB1 with 100nF+1uF.",
    "MCLR: R3 10k pull-up, R7 470R isolates C9/SW1 from PICkit VPP. ICSP uses PGD3/PGC3 (RB5/RB6).",
    "USER LED D3 on RC13 (active low), USER button SW2 on RC12 (active low, R6 pull-up).",
    "J5 UART (3.3V logic): 1=3V3 2=TX=RC10/RP58 (U1TX via PPS) 3=RX=RC11/RP59 (U1RX via PPS) 4=GND.",
]
for i, n in enumerate(notes):
    items.append(f'(text {q(n)} (exclude_from_sim no) (at 20 {240 + i * 6} 0) (effects (font (size 1.8 1.8)) (justify left bottom)) (uuid {U("note", i)}))')

sch = (f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid {ROOT_UUID}) (paper "A3") '
       f'(title_block (title "dsPIC33CK256MC005 Nano board") (date "2026-11-10") (rev "A") (company "") (comment 1 "USB-C powered, MCP1703A-3302 LDO")) '
       f'(lib_symbols {" ".join(lib_syms.values())}) {" ".join(items)} '
       f'(sheet_instances (path "/" (page "1"))) (embedded_fonts no))')

os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, PROJECT + ".kicad_sch"), "w", encoding="utf-8").write(sch)
open(os.path.join(OUT, "Board.kicad_sym"), "w", encoding="utf-8").write(
    '(kicad_symbol_lib (version 20241209) (generator "gen_sch") ' + " ".join(
        s.replace("(symbol \"Board:", "(symbol \"", 1) for s in CUSTOM.values()) + ")")
open(os.path.join(OUT, "sym-lib-table"), "w").write(
    '(sym_lib_table (version 7) (lib (name "Board") (type "KiCad") (uri "${KIPRJMOD}/Board.kicad_sym") (options "") (descr "")))\n')
print("schematic written:", len(PARTS), "parts")
