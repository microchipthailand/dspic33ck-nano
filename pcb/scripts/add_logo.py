"""Add Microchip logo (F.SilkS, board-only footprint LOGO1) right of USB-C and set red solder mask.
Idempotent: removes an existing LOGO1 and replaces the stackup block.
usage: python -I add_logo.py board.kicad_pcb logo.png [cx cy width_mm]"""
import sys, re, pcbnew
from PIL import Image
import numpy as np

path, png = sys.argv[1], sys.argv[2]
cx, cy, W = (float(v) for v in sys.argv[3:6]) if len(sys.argv) > 5 else (26.25, 3.6, 8.6)
PX = 0.025  # mm per pixel

im = Image.open(png).convert("RGBA")
a = np.array(im).astype(int)
ink = (a[..., 3] > 128) & (a[..., :3].min(-1) < 200)       # red + black = ink
ys, xs = np.nonzero(ink)
im = im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
nw = int(round(W / PX)); nh = int(round(im.height * nw / im.width))
a = np.array(im.resize((nw, nh), Image.LANCZOS)).astype(int)
ink = (a[..., 3] > 128) & (a[..., :3].min(-1) < 200)
H = nh * PX
print(f"logo {W:.2f} x {H:.2f} mm at ({cx},{cy})")

M = pcbnew.FromMM
poly = pcbnew.SHAPE_POLY_SET()
for r in range(nh):
    row = ink[r]; c = 0
    while c < nw:
        if row[c]:
            s = c
            while c < nw and row[c]: c += 1
            x0, x1 = cx - W/2 + s*PX, cx - W/2 + c*PX
            y0, y1 = cy - H/2 + r*PX, cy - H/2 + (r+1)*PX
            ch = pcbnew.SHAPE_LINE_CHAIN()
            for x, y in ((x0,y0),(x1,y0),(x1,y1),(x0,y1)):
                ch.Append(M(x), M(y))
            ch.SetClosed(True)
            poly.AddOutline(ch)
        else:
            c += 1
poly.Simplify()
poly.Fracture()

board = pcbnew.LoadBoard(path)
for fp in list(board.GetFootprints()):
    if fp.GetReference() == "LOGO1":
        board.Remove(fp)
fp = pcbnew.FOOTPRINT(board)
fp.SetReference("LOGO1"); fp.SetValue("Microchip_Logo")
fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
fp.Reference().SetLayer(pcbnew.F_Fab); fp.Value().SetLayer(pcbnew.F_Fab)
fp.SetAttributes(pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
fp.SetPosition(pcbnew.VECTOR2I(M(cx), M(cy)))
fp.SetLocked(True)
for i in range(poly.OutlineCount()):
    sh = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_POLY)
    one = pcbnew.SHAPE_POLY_SET(); one.AddOutline(poly.Outline(i))
    sh.SetPolyShape(one); sh.SetFilled(True); sh.SetWidth(0)
    sh.SetLayer(pcbnew.F_SilkS)
    fp.Add(sh)
board.Add(fp)
pcbnew.SaveBoard(path, board)
print("outlines", poly.OutlineCount(), "points", poly.TotalVertices())

# ---- red solder mask stackup ----
STACK = """\t\t(stackup
\t\t\t(layer "F.SilkS" (type "Top Silk Screen") (color "White"))
\t\t\t(layer "F.Paste" (type "Top Solder Paste"))
\t\t\t(layer "F.Mask" (type "Top Solder Mask") (color "Red") (thickness 0.01))
\t\t\t(layer "F.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "dielectric 1" (type "core") (thickness 1.51) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
\t\t\t(layer "B.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "B.Mask" (type "Bottom Solder Mask") (color "Red") (thickness 0.01))
\t\t\t(layer "B.Paste" (type "Bottom Solder Paste"))
\t\t\t(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
\t\t\t(copper_finish "HAL lead-free")
\t\t\t(dielectric_constraints no)
\t\t)
"""
s = open(path, encoding="utf-8").read()
m = re.search(r"\n\t\t\(stackup\n.*?\n\t\t\)\n", s, re.S)
if m:
    s = s[:m.start()+1] + STACK + s[m.end():]
else:
    s = s.replace("\t(setup\n", "\t(setup\n" + STACK, 1)
open(path, "w", encoding="utf-8").write(s)
print("stackup: red mask set")
