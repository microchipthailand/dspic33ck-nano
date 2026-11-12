import sys, pcbnew
fp = pcbnew.FootprintLoad(sys.argv[1], sys.argv[2])
for g in fp.GraphicalItems():
    if g.GetLayerName() in ("Dwgs.User","Cmts.User") and hasattr(g,"GetStart"):
        print(g.GetLayerName(), g.GetClass(), [round(pcbnew.ToMM(v),2) for v in (g.GetStart().x,g.GetStart().y,g.GetEnd().x,g.GetEnd().y)])
