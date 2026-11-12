"""Stitch every F.Cu GND pour island to the B.Cu GND plane, and fix any track narrower than the minimum.
For each filled island on F.Cu that has no GND via/through-hole pad in it, place a via at a point inside
the island that is clear of other-net copper on both layers. Repeats until no new vias are needed.
Also adds a via grid along the board for good top/bottom coupling.
Usage: python.exe -I stitch_pour.py board.kicad_pcb
"""
import sys
import pcbnew

PATH = sys.argv[1]
MM, TO = pcbnew.FromMM, pcbnew.ToMM
VIA_D, VIA_DR, CLR = 0.6, 0.3, 0.25
MIN_W = 0.15

b = pcbnew.LoadBoard(PATH)
gnd = b.FindNet("/GND")
for z in b.Zones():          # drop pour fragments that do not reach a GND pad (no floating copper)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)

# 1. widen any sub-minimum track segment (router neckdown artefacts)
fixed = 0
for t in b.GetTracks():
    if t.GetClass() == "PCB_TRACK" and t.GetWidth() < MM(MIN_W):
        t.SetWidth(MM(MIN_W)); fixed += 1
print("widened tracks:", fixed)

def other_copper():
    obs = []
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.GetNetCode() != gnd.GetNetCode():
                for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                    if p.IsOnLayer(L):
                        obs.append((L, p.GetEffectiveShape(L)))
    for t in b.GetTracks():
        if t.GetNetCode() != gnd.GetNetCode():
            for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                if t.IsOnLayer(L):
                    obs.append((L, t.GetEffectiveShape(L)))
    return obs


def gnd_vias():
    pts = [t.GetPosition() for t in b.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetCode() == gnd.GetNetCode()]
    pts += [p.GetPosition() for f in b.GetFootprints() for p in f.Pads()
            if p.GetNetCode() == gnd.GetNetCode() and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    return pts


EDGE = b.GetBoardEdgesBoundingBox()
EM = MM(0.3 + VIA_D / 2 + 0.05)          # copper-to-edge rule + via radius


def try_via(x, y, obs, existing):
    if not (EDGE.GetLeft() + EM < x < EDGE.GetRight() - EM and EDGE.GetTop() + EM < y < EDGE.GetBottom() - EM):
        return None
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(x, y)); v.SetWidth(MM(VIA_D)); v.SetDrill(MM(VIA_DR))
    v.SetNet(gnd); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    for L, s in obs:
        if s.Collide(v.GetEffectiveShape(L), MM(CLR)):
            return None
    for e in existing:
        if (e - v.GetPosition()).EuclideanNorm() < MM(VIA_D + 0.4):
            return None
    return v


def top_zone():
    return [z for z in b.Zones() if z.GetNetCode() == gnd.GetNetCode() and z.IsOnLayer(pcbnew.F_Cu)][0]


def bottom_fill():
    return [z for z in b.Zones() if z.GetNetCode() == gnd.GetNetCode() and z.IsOnLayer(pcbnew.B_Cu)][0].GetFilledPolysList(pcbnew.B_Cu)


added = 0
for rnd in range(6):
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    fill = top_zone().GetFilledPolysList(pcbnew.F_Cu)
    bfill = bottom_fill()
    obs, existing = other_copper(), gnd_vias()
    new = 0
    for i in range(fill.OutlineCount()):
        island = pcbnew.SHAPE_POLY_SET(); island.AddOutline(fill.Outline(i))
        for h in range(fill.HoleCount(i)):
            island.AddHole(fill.Hole(i, h))
        if any(island.Contains(p) for p in existing):
            continue
        bb = island.BBox()
        step = MM(0.25)
        done = False
        y = bb.GetTop() + step
        while y < bb.GetBottom() and not done:
            x = bb.GetLeft() + step
            while x < bb.GetRight() and not done:
                pt = pcbnew.VECTOR2I(x, y)
                # via pad must sit fully inside the island and on the bottom plane
                if island.Contains(pt, -1, MM(VIA_D / 2 + 0.05)) is False:
                    pass
                if island.Contains(pt) and bfill.Contains(pt):
                    v = try_via(x, y, obs, existing)
                    if v is not None:
                        b.Add(v); existing.append(v.GetPosition()); new += 1; done = True
                x += step
            y += step
    added += new
    if new == 0:
        break

# 2. general stitching grid every ~5 mm where there is room
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
fill = top_zone().GetFilledPolysList(pcbnew.F_Cu); bfill = bottom_fill()
obs, existing = other_copper(), gnd_vias()
edge = b.GetBoardEdgesBoundingBox()
grid = 0
y = edge.GetTop() + MM(2.5)
while y < edge.GetBottom() - MM(2):
    x = edge.GetLeft() + MM(3.5)
    while x < edge.GetRight() - MM(3):
        pt = pcbnew.VECTOR2I(x, y)
        if fill.Contains(pt) and bfill.Contains(pt) and all((e - pt).EuclideanNorm() > MM(3.5) for e in existing):
            v = try_via(x, y, obs, existing)
            if v is not None:
                b.Add(v); existing.append(pt); grid += 1
        x += MM(1.0)
    y += MM(1.0)

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PATH, b)
print("island stitch vias:", added, " grid stitch vias:", grid)
