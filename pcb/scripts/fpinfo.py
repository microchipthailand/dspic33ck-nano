import sys, pcbnew
lib, name = sys.argv[1], sys.argv[2]
fp = pcbnew.FootprintLoad(lib, name)
mm = pcbnew.ToMM
for p in fp.Pads():
    print("pad", p.GetNumber(), "at", round(mm(p.GetPosition().x),3), round(mm(p.GetPosition().y),3), "size", round(mm(p.GetSize().x),2), round(mm(p.GetSize().y),2), "drill", round(mm(p.GetDrillSize().x),2))
bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox() if hasattr(fp, "GetCourtyard") else None
if bb: print("courtyard", mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
for g in fp.GraphicalItems():
    if g.GetLayerName() == "Dwgs.User" and g.GetClass() != "PCB_TEXT":
        print("dwg", round(mm(g.GetStart().x),2), round(mm(g.GetStart().y),2), round(mm(g.GetEnd().x),2), round(mm(g.GetEnd().y),2))
