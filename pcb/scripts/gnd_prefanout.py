"""Pre-route GND escapes so every GND SMD pad has its own via to the B.Cu GND plane.

  * U1 (TQFP-48) GND pins 14/19/31/42: stub inward from the pad tip under the package body to a via
    (0.6/0.3 mm). The area under a TQFP body is free of copper and silkscreen and is tented by the part.
  * Other GND SMD pads (passives, LED, LDO, button, USB): stub outward (away from the part's other pads)
    to a via placed 0.9-1.6 mm away, searching for a spot that clears other-net pads.
Items are locked so Freerouting keeps them. Usage: python.exe -I gnd_prefanout.py board.kicad_pcb
"""
import math, sys
import pcbnew

PATH = sys.argv[1]
MM, TO = pcbnew.FromMM, pcbnew.ToMM
VIA_D, VIA_DR, STUB_W, CLR = 0.6, 0.3, 0.3, 0.25

b = pcbnew.LoadBoard(PATH)
gnd = b.FindNet("/GND")
for t in list(b.GetTracks()):
    b.Remove(t)

other_pads = [p for f in b.GetFootprints() for p in f.Pads() if p.GetNetCode() != gnd.GetNetCode()]
placed_vias = []          # (x, y) of vias added so far (mm)
edge = b.GetBoardEdgesBoundingBox()
L, T, R, B = (TO(v) for v in (edge.GetLeft(), edge.GetTop(), edge.GetRight(), edge.GetBottom()))


def add(pad, vx, vy):
    via = pcbnew.PCB_VIA(b)
    via.SetPosition(pcbnew.VECTOR2I(MM(vx), MM(vy)))
    via.SetWidth(MM(VIA_D)); via.SetDrill(MM(VIA_DR)); via.SetNet(gnd)
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); via.SetLocked(True)
    trk = pcbnew.PCB_TRACK(b)
    trk.SetStart(pad.GetPosition()); trk.SetEnd(via.GetPosition())
    trk.SetWidth(MM(STUB_W)); trk.SetLayer(pcbnew.F_Cu); trk.SetNet(gnd); trk.SetLocked(True)
    return via, trk


def clear(via, trk, inward=False):
    vs, ts = via.GetEffectiveShape(pcbnew.F_Cu), trk.GetEffectiveShape(pcbnew.F_Cu)
    if inward:   # QFP inward stub: runs straight along its own pad axis, so only the via spot must be clear
        ts = None
    for p in other_pads:
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            if not p.IsOnLayer(layer):
                continue
            ps = p.GetEffectiveShape(layer)
            if ps.Collide(vs, MM(CLR)) or (ts is not None and layer == pcbnew.F_Cu and ps.Collide(ts, MM(CLR))):
                return False
    vx, vy = TO(via.GetPosition().x), TO(via.GetPosition().y)
    if any(math.hypot(vx - x, vy - y) < VIA_D + 0.3 for x, y in placed_vias):
        return False
    return L + 0.9 < vx < R - 0.9 and T + 0.9 < vy < B - 0.9


report = []
u1 = b.FindFootprintByReference("U1")
cx, cy = TO(u1.GetPosition().x), TO(u1.GetPosition().y)

for fp in b.GetFootprints():
    for pad in fp.Pads():
        if pad.GetNetCode() != gnd.GetNetCode() or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        px, py = TO(pad.GetPosition().x), TO(pad.GetPosition().y)
        name = f"{fp.GetReference()}.{pad.GetNumber()}"
        if fp.GetReference() == "U1":
            # inward toward chip centre: pad centre is ~4.16 mm from centre; body edge ~3.5 mm; go to ~2.3 mm
            d = math.hypot(px - cx, py - cy); ux, uy = (cx - px) / d, (cy - py) / d
            # snap to the pad axis (QFP side pads are axis-aligned) so the stub stays inside the pad column
            if abs(px - cx) > abs(py - cy):
                ux, uy = (1 if cx > px else -1), 0
            else:
                ux, uy = 0, (1 if cy > py else -1)
            cands = [(px + ux * s, py + uy * s) for s in (2.0, 2.3, 2.6)]
        else:
            fx, fy = TO(fp.GetPosition().x), TO(fp.GetPosition().y)
            base = math.atan2(py - fy, px - fx) if math.hypot(px - fx, py - fy) > 0.05 else 0.0
            cands = [(px + r * math.cos(base + math.radians(a)), py + r * math.sin(base + math.radians(a)))
                     for r in (0.9, 1.1, 1.3, 1.6) for a in (0, 30, -30, 60, -60, 90, -90, 120, -120, 180)]
        for vx, vy in cands:
            via, trk = add(pad, vx, vy)
            if clear(via, trk, inward=(fp.GetReference() == "U1")):
                b.Add(trk); b.Add(via)
                placed_vias.append((vx, vy))
                report.append(f"{name}: via at ({vx:.2f}, {vy:.2f})")
                break
        else:
            report.append(f"{name}: NO SPOT")

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PATH, b)
print("\n".join(report))
bad = [r for r in report if "NO SPOT" in r]
print(f"{len(report) - len(bad)}/{len(report)} GND pads escaped")
sys.exit(1 if bad else 0)
