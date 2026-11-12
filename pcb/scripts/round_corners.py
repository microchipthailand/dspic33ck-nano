"""Replace rectangular Edge.Cuts outline with a rounded-corner rectangle and refill zones.
usage: python -I round_corners.py board.kicad_pcb [radius_mm]"""
import sys, math, pcbnew
path = sys.argv[1]; R = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
M = pcbnew.FromMM
b = pcbnew.LoadBoard(path)
bb = b.GetBoardEdgesBoundingBox()
# bbox includes half line width; outline lines are 0.1 mm wide
x0 = round(pcbnew.ToMM(bb.GetX()) + 0.05, 3); y0 = round(pcbnew.ToMM(bb.GetY()) + 0.05, 3)
x1 = round(pcbnew.ToMM(bb.GetRight()) - 0.05, 3); y1 = round(pcbnew.ToMM(bb.GetBottom()) - 0.05, 3)
for d in list(b.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        b.Remove(d)
def V(x, y): return pcbnew.VECTOR2I(M(x), M(y))
def add(shape):
    shape.SetLayer(pcbnew.Edge_Cuts); shape.SetWidth(M(0.1)); b.Add(shape)
def line(a, c):
    s = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_SEGMENT); s.SetStart(V(*a)); s.SetEnd(V(*c)); add(s)
def arc(cx, cy, a0):  # quarter arc, center (cx,cy), start angle a0 deg, sweeping +90 (clockwise on screen, y down)
    s = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_ARC)
    pts = [(cx + R*math.cos(math.radians(a)), cy + R*math.sin(math.radians(a))) for a in (a0, a0+45, a0+90)]
    s.SetArcGeometry(V(*pts[0]), V(*pts[1]), V(*pts[2])); add(s)
line((x0+R, y0), (x1-R, y0)); arc(x1-R, y0+R, 270)
line((x1, y0+R), (x1, y1-R)); arc(x1-R, y1-R, 0)
line((x1-R, y1), (x0+R, y1)); arc(x0+R, y1-R, 90)
line((x0, y1-R), (x0, y0+R)); arc(x0+R, y0+R, 180)
# zones refilled afterwards by: kicad-cli pcb drc --refill-zones --save-board
pcbnew.SaveBoard(path, b)
print(f"outline {x0},{y0} - {x1},{y1} mm, corner radius {R} mm")
