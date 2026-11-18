import sys, pcbnew
fp = pcbnew.FootprintLoad(sys.argv[1], sys.argv[2])
mm=pcbnew.ToMM
for g in fp.GraphicalItems():
    if g.GetClass()=="PCB_SHAPE" and g.GetLayerName() in ("Dwgs.User","Cmts.User","F.Fab"):
        print(g.GetLayerName(), g.GetShapeStr(), round(mm(g.GetStart().x),2), round(mm(g.GetStart().y),2), round(mm(g.GetEnd().x),2), round(mm(g.GetEnd().y),2))
