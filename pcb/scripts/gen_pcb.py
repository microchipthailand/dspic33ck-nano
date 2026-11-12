"""Place footprints for the dsPIC33CK Nano board and write the .kicad_pcb.
Run with KiCad's python:  python.exe -I gen_pcb.py <kicad_share_dir> <project_dir>
"""
import os, sys, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import PARTS, LEFT, RIGHT

SHARE, OUT = sys.argv[1], sys.argv[2]
PROJECT = "dspic33ck_nano"
FPDIR = os.path.join(SHARE, "footprints")
MM = pcbnew.FromMM
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))
sym_uuid = lambda ref: str(uuid.uuid5(uuid.NAMESPACE_DNS, PROJECT + "/" + ref))

OX = 3.0                   # label strip outside each header row (mm)
W, H = 25.4 + 2 * OX, 74.5 # board size (mm); headers 0.9" apart
HX_L, HX_R = OX + 1.27, OX + 1.27 + 22.86
HY0 = 10.0                 # first header pin y

board = pcbnew.BOARD()
nets = {}


def net(name):
    if name not in nets:
        ni = pcbnew.NETINFO_ITEM(board, "/" + name)   # root-sheet local labels are "/NAME" in the schematic netlist
        board.Add(ni)
        nets[name] = ni
    return nets[name]


fps = {}
for ref, lib_id, value, fpid, pins, _, mpn in PARTS:
    if ref.startswith("#"):
        continue
    lib, name = fpid.split(":")
    fp = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + ".pretty"), name)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetPath(pcbnew.KIID_PATH("/" + sym_uuid(ref)))
    if mpn:
        fp.SetField("MPN", mpn)
    for f in fp.GetFields():                      # silkscreen: small reference only; value/MPN hidden
        if f.GetName() == "Reference":
            f.SetTextSize(V(0.7, 0.7)); f.SetTextThickness(MM(0.12)); f.SetVisible(True)
        else:
            f.SetVisible(False)
    for g in list(fp.GraphicalItems()):           # drop footprint-internal user text that lands on silk
        if g.GetClass() == "PCB_TEXT" and g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            fp.Remove(g)
    for pad in fp.Pads():
        n = pins.get(pad.GetNumber())
        if n:
            pad.SetNet(net(n))
    board.Add(fp)
    fps[ref] = fp


def place(ref, x, y, rot=0, absolute=False):
    if not absolute:
        x += OX                # layout below was drawn for the 25.4 mm board; shift into the wider one
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(V(x, y))


def pad_xy(ref, num):
    for p in fps[ref].Pads():
        if p.GetNumber() == str(num):
            return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
    raise KeyError((ref, num))


# ---- headers (breadboard pitch) ----
place("J2", HX_L, HY0, absolute=True)
place("J3", HX_R, HY0, absolute=True)
BOTTOM_SIDE = ("J2", "J3", "J4", "J5")   # pin headers mounted on B side, flipped after placement

# ---- USB-C flush with top edge: footprint edge line is at local y=+3.4, rotate 180 -> world y = fy-3.4 ----
place("J1", W / 2, 3.4, 180, absolute=True)
place("R1", 9.2, 9.4, 90)       # CC1 / CC2 pulldowns right under the connector
place("R2", 16.2, 9.4, 90)
place("D1", 19.6, 13.0, 90)     # Schottky VBUS -> +5V
place("C1", 16.4, 13.4, 90)
place("U2", 12.0, 15.6, 180)    # MCP1703A SOT-89
place("C2", 7.4, 13.4, 90)
place("C3", 7.4, 17.8, 90)
place("R4", 18.6, 18.6, 0)      # power LED
place("D2", 18.6, 20.8, 180)

# ---- MCU in the middle ----
MX, MY = W / 2, 33.0
place("U1", MX, MY, 0, absolute=True)

# Decoupling: one 100nF right outside each VDD pin.  Pin 18 bottom side, 32 right side, 43 top side.
x18, y18 = pad_xy("U1", 18)
place("C4", x18 + 0.6, y18 + 2.9, 0, absolute=True)
x32, y32 = pad_xy("U1", 32)
place("C5", x32 + 3.0, y32 + 0.5, 90, absolute=True)
x43, y43 = pad_xy("U1", 43)
place("C6", x43 + 0.6, y43 - 2.9, 180, absolute=True)
# AVDD (13) / AVSS (14) bottom-left corner: FB1 + 100n + 1u
x13, y13 = pad_xy("U1", 13)
place("C7", x13 - 1.6, y13 + 2.9, 0, absolute=True)
place("C8", x13 - 1.6, y13 + 5.0, 0, absolute=True)
place("FB1", x13 + 2.1, y13 + 5.0, 0, absolute=True)

# MCLR network left of the chip (pin 5 on the left side)
place("R3", 4.6, 27.2, 90)
place("R7", 4.6, 31.3, 90)
place("C9", 4.6, 35.4, 90)

# Buttons + user LED/pull-up in the lower area
place("SW1", 7.9, 47.0, 0)      # RESET
place("SW2", 17.5, 47.0, 0)     # USER
place("R6", 21.2, 53.2, 90)
place("R5", 8.0, 54.9, 0)
place("D3", 8.0, 57.1, 180)

# ICSP header across the bottom, below the side headers (PICkit pin 1 = MCLR on the left)
place("J4", 6.35, 66.6, 90)
# UART header below the ICSP header, centred: 1=3V3 2=TX(RC10) 3=RX(RC11) 4=GND
place("J5", 8.89, 71.7, 90)

# ---- board outline ----
def edge(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1)); s.SetEnd(V(x2, y2))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)

edge(0, 0, W, 0); edge(W, 0, W, H); edge(W, H, 0, H); edge(0, H, 0, 0)

# ---- GND pours both layers ----
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net("GND"))
    z.SetLocalClearance(MM(0.3))
    z.SetMinThickness(MM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(MM(0.3))
    z.SetThermalReliefSpokeWidth(MM(0.4))
    ol = z.Outline(); ol.NewOutline()
    for x, y in ((0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)):
        ol.Append(MM(x), MM(y))
    board.Add(z)

# ---- pin labels on bottom silkscreen (mirrored reading from below) and top for power pins ----
def text(s, x, y, layer, size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_CENTER, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(V(size, size)); t.SetTextThickness(MM(max(0.12, size * 0.15)))
    t.SetHorizJustify(just); t.SetMirrored(mirror)
    board.Add(t)

for i, n in enumerate(LEFT):
    text(n.replace("+", ""), HX_L - 1.1, HY0 + i * 2.54, pcbnew.F_SilkS, 0.7, just=pcbnew.GR_TEXT_H_ALIGN_RIGHT)
for i, n in enumerate(RIGHT):
    text(n.replace("+", ""), HX_R + 1.1, HY0 + i * 2.54, pcbnew.F_SilkS, 0.7, just=pcbnew.GR_TEXT_H_ALIGN_LEFT)
text("dsPIC33CK256MC005", W / 2, 61.0, pcbnew.F_SilkS, 0.9)
text("Nano  rev A", W / 2, 62.4, pcbnew.F_SilkS, 0.8)
for i, lbl in enumerate(("MCLR", "3V3", "GND", "PGD", "PGC")):   # PICkit pinout above J4 (pin 1 = MCLR)
    text(lbl, OX + 6.35 + i * 2.54, 64.5, pcbnew.F_SilkS, 0.6)
text("ICSP", 21.6 + OX, 66.6, pcbnew.F_SilkS, 0.6)
for i, lbl in enumerate(("3V3", "TX", "RX", "GND")):
    text(lbl, OX + 8.89 + i * 2.54, 69.4, pcbnew.F_SilkS, 0.6)
text("UART", 20.3 + OX, 71.7, pcbnew.F_SilkS, 0.6)
text("RESET", 7.9 + OX, 51.2, pcbnew.F_SilkS, 0.8)
text("USER", 15.6 + OX, 51.2, pcbnew.F_SilkS, 0.8)
text("LED", 11.0 + OX, 56.0, pcbnew.F_SilkS, 0.8)
text("PWR", 21.0 + OX, 21.8, pcbnew.F_SilkS, 0.6)

# ---- design rules: JLCPCB/PCBWay-friendly 2-layer, 1.6mm ----
def apply_rules(b):
    ds = b.GetDesignSettings()
    ds.m_TrackMinWidth = MM(0.15)
    ds.m_MinClearance = MM(0.15)
    ds.m_ViasMinSize = MM(0.5)
    ds.m_MinThroughDrill = MM(0.3)
    ds.m_CopperEdgeClearance = MM(0.3)
    ds.m_HoleToHoleMin = MM(0.5)
    ds.m_SilkClearance = MM(0.0)
    ns = ds.m_NetSettings
    d = ns.GetDefaultNetclass()
    d.SetTrackWidth(MM(0.2)); d.SetClearance(MM(0.2)); d.SetViaDiameter(MM(0.6)); d.SetViaDrill(MM(0.3))
    p = pcbnew.NETCLASS("Power")
    p.SetTrackWidth(MM(0.4)); p.SetClearance(MM(0.15)); p.SetViaDiameter(MM(0.8)); p.SetViaDrill(MM(0.4))
    ns.SetNetclass("Power", p)
    for n in ("VBUS", "+5V", "+3V3", "+3V3A", "GND"):
        ns.SetNetclassPatternAssignment("/" + n, "Power")


# Flip pin headers to B side. Choose the rotation that keeps every pad where it was before the flip,
# so header pin order (and the silkscreen labels) are unchanged; verify exactly.
for ref in BOTTOM_SIDE:
    fp = fps[ref]
    before = {q.GetNumber(): (q.GetPosition().x, q.GetPosition().y) for q in fp.Pads()}
    fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    for rot in (0, 90, 180, 270):
        fp.SetOrientationDegrees(rot)
        after = {q.GetNumber(): (q.GetPosition().x, q.GetPosition().y) for q in fp.Pads()}
        if all(abs(after[k][0] - v[0]) < 1000 and abs(after[k][1] - v[1]) < 1000 for k, v in before.items()):
            break
    else:
        raise SystemExit(f"{ref}: no rotation keeps pad positions after flip")
    assert fp.GetLayer() == pcbnew.B_Cu, ref

# reference designator tweaks where defaults collide
def ref_at(ref, dx, dy, visible=True):
    f = fps[ref].Reference()
    f.SetVisible(visible)
    p = fps[ref].GetPosition()
    f.SetPosition(pcbnew.VECTOR2I(p.x + MM(dx), p.y + MM(dy)))
    f.SetTextAngleDegrees(0)
ref_at("U1", 0, 0, visible=False)           # obvious part; keeps silk off the pads
ref_at("C6", 2.6, 0)
ref_at("C8", -2.6, 0)
ref_at("C7", -2.6, 0)
ref_at("FB1", 0, 1.5)
ref_at("C4", 2.6, 0)
ref_at("SW1", 0, -3.9)
ref_at("SW2", 0, -3.9)
ref_at("R6", 0, 2.2)

path = os.path.join(OUT, PROJECT + ".kicad_pcb")
board.SetFileName(path)
pcbnew.SaveBoard(path, board)
# Rules live in the .kicad_pro; reload so the project is attached, apply, fill zones, save board + project.
board = pcbnew.LoadBoard(path)
apply_rules(board)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
pcbnew.SaveBoard(path, board)
pcbnew.SaveProjectSettings() if hasattr(pcbnew, "SaveProjectSettings") else pcbnew.GetSettingsManager().SaveProject()
print("pcb written", path, "footprints:", len(fps))
