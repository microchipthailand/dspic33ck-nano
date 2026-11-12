"""Report GND connectivity groups: union-find over GND pads, tracks, vias and zone fill fragments.
Usage: python.exe -I gnd_islands.py board.kicad_pcb
"""
import sys
import pcbnew

b = pcbnew.LoadBoard(sys.argv[1])
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
g = b.FindNet("/GND").GetNetCode()
TO = pcbnew.ToMM
items = []   # (label, layer_set, shape_per_layer dict)

for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetCode() == g:
            items.append((f"pad {f.GetReference()}.{p.GetNumber()}",
                          {L: p.GetEffectiveShape(L) for L in (pcbnew.F_Cu, pcbnew.B_Cu) if p.IsOnLayer(L)}))
for t in b.GetTracks():
    if t.GetNetCode() == g:
        kind = "via" if t.GetClass() == "PCB_VIA" else "trk"
        items.append((f"{kind} ({TO(t.GetPosition().x):.2f},{TO(t.GetPosition().y):.2f})",
                      {L: t.GetEffectiveShape(L) for L in (pcbnew.F_Cu, pcbnew.B_Cu) if t.IsOnLayer(L)}))
for z in b.Zones():
    if z.GetNetCode() != g:
        continue
    for L in (pcbnew.F_Cu, pcbnew.B_Cu):
        if not z.IsOnLayer(L):
            continue
        fill = z.GetFilledPolysList(L)
        for i in range(fill.OutlineCount()):
            s = pcbnew.SHAPE_POLY_SET(); s.AddOutline(fill.Outline(i))
            for h in range(fill.HoleCount(i)):
                s.AddHole(fill.Hole(i, h))
            bb = s.BBox()
            items.append((f"pour {'F' if L == pcbnew.F_Cu else 'B'} #{i} bbox({TO(bb.GetLeft()):.1f},{TO(bb.GetTop()):.1f})-({TO(bb.GetRight()):.1f},{TO(bb.GetBottom()):.1f}) area {s.Area() / 1e12:.2f}mm2",
                          {L: s}))

parent = list(range(len(items)))
def find(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]; i = parent[i]
    return i

def touch(a, b_):
    for L, sa in a.items():
        sb = b_.get(L)
        if sb is None:
            continue
        if isinstance(sa, pcbnew.SHAPE_POLY_SET) and isinstance(sb, pcbnew.SHAPE_POLY_SET):
            continue
        if isinstance(sa, pcbnew.SHAPE_POLY_SET):
            if sa.Collide(sb, 0): return True
        elif isinstance(sb, pcbnew.SHAPE_POLY_SET):
            if sb.Collide(sa, 0): return True
        elif sa.Collide(sb, 0):
            return True
    return False

for i in range(len(items)):
    for j in range(i + 1, len(items)):
        if find(i) != find(j) and touch(items[i][1], items[j][1]):
            parent[find(i)] = find(j)

groups = {}
for i in range(len(items)):
    groups.setdefault(find(i), []).append(items[i][0])
gs = sorted(groups.values(), key=len, reverse=True)
print("GND groups:", len(gs), "sizes", [len(x) for x in gs])
for grp in gs[1:]:
    print("  isolated:", grp[:6])
sys.exit(0 if len(gs) == 1 else 1)
