"""Give isolated GND pads a short stub + via to the B.Cu GND plane.
For each GND SMD pad on F.Cu that the F.Cu pour does not reach, try candidate via spots moving away from
the pad (preferring the direction away from its footprint centre), and keep the first one whose
stub + via clears all other-net copper by >= CLEAR. Then refill zones and save.
Usage: python.exe -I gnd_stitch.py board.kicad_pcb
"""
import math, sys
import pcbnew

PATH = sys.argv[1]
MM, TO = pcbnew.FromMM, pcbnew.ToMM
CLEAR = 0.2          # mm, signal clearance
VIA_D, VIA_DR = 0.6, 0.3
STUB_W = 0.3

b = pcbnew.LoadBoard(PATH)
gnd = b.FindNet("/GND")
pcbnew.ZONE_FILLER(b).Fill(b.Zones())

# Board outline for edge clearance
edge = b.GetBoardEdgesBoundingBox()


def pad_connected_to_pour(pad):
    """True if F.Cu GND zone fill touches the pad."""
    for z in b.Zones():
        if z.GetNetCode() != gnd.GetNetCode() or not z.IsOnLayer(pcbnew.F_Cu):
            continue
        fill = z.GetFilledPolysList(pcbnew.F_Cu)
        poly = pad.GetEffectivePolygon(pcbnew.F_Cu)
        if fill.Collide(poly.Outline(0), 0) if poly.OutlineCount() else False:
            return True
        if fill.Collide(pad.GetPosition(), 0):
            return True
    return False


def obstacles():
    """Other-net copper shapes per layer as (shape, layer)."""
    obs = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetCode() == gnd.GetNetCode():
                continue
            for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                if p.IsOnLayer(L):
                    obs.append((p.GetEffectiveShape(L), L))
    for t in b.GetTracks():
        if t.GetNetCode() == gnd.GetNetCode():
            continue
        for L in (pcbnew.F_Cu, pcbnew.B_Cu):
            if t.IsOnLayer(L):
                obs.append((t.GetEffectiveShape(L), L))
    return obs


def clear_of(shape, layer, obs, clr):
    for s, L in obs:
        if L == layer and shape.Collide(s, MM(clr)):
            return False
    return True


def add_stitch(pad, obs):
    px, py = TO(pad.GetPosition().x), TO(pad.GetPosition().y)
    fp = pad.GetParentFootprint()
    cx, cy = TO(fp.GetPosition().x), TO(fp.GetPosition().y)
    base = math.atan2(py - cy, px - cx) if (px, py) != (cx, cy) else 0.0
    # snap the outward direction to the nearest axis for QFP side pads (clean stubs)
    angs = [base + math.radians(d) for d in (0, 20, -20, 45, -45, 70, -70, 90, -90, 135, -135, 180)]
    for dist in (1.0, 1.3, 1.6, 2.0, 2.5, 3.0):
        for a in angs:
            vx, vy = px + dist * math.cos(a), py + dist * math.sin(a)
            if not (TO(edge.GetLeft()) + 0.8 < vx < TO(edge.GetRight()) - 0.8 and TO(edge.GetTop()) + 0.8 < vy < TO(edge.GetBottom()) - 0.8):
                continue
            via = pcbnew.PCB_VIA(b)
            via.SetPosition(pcbnew.VECTOR2I(MM(vx), MM(vy)))
            via.SetWidth(MM(VIA_D)); via.SetDrill(MM(VIA_DR)); via.SetNet(gnd)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            trk = pcbnew.PCB_TRACK(b)
            trk.SetStart(pad.GetPosition()); trk.SetEnd(via.GetPosition())
            trk.SetWidth(MM(STUB_W)); trk.SetLayer(pcbnew.F_Cu); trk.SetNet(gnd)
            ok = (clear_of(trk.GetEffectiveShape(pcbnew.F_Cu), pcbnew.F_Cu, obs, CLEAR)
                  and clear_of(via.GetEffectiveShape(pcbnew.F_Cu), pcbnew.F_Cu, obs, CLEAR)
                  and clear_of(via.GetEffectiveShape(pcbnew.B_Cu), pcbnew.B_Cu, obs, CLEAR))
            if ok:
                b.Add(trk); b.Add(via)
                obs_hole = via.GetEffectiveShape(pcbnew.F_Cu)
                return (round(vx, 2), round(vy, 2), round(dist, 2))
    return None


targets = []
for fp in b.GetFootprints():
    for p in fp.Pads():
        if p.GetNetCode() == gnd.GetNetCode() and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and not pad_connected_to_pour(p):
            targets.append(p)

obs = obstacles()
failed = []
for p in targets:
    r = add_stitch(p, obs)
    name = f"{p.GetParentFootprint().GetReference()}.{p.GetNumber()}"
    print(("stitched " + name + " via at " + str(r)) if r else ("FAILED " + name))
    if not r:
        failed.append(name)

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PATH, b)
print(f"{len(targets) - len(failed)}/{len(targets)} isolated GND pads stitched")
sys.exit(1 if failed else 0)
