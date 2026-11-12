import pcbnew, sys
b=pcbnew.LoadBoard(sys.argv[1]); mm=pcbnew.ToMM
bb=b.GetBoardEdgesBoundingBox(); print("board",mm(bb.GetX()),mm(bb.GetY()),mm(bb.GetRight()),mm(bb.GetBottom()))
for fp in b.GetFootprints():
    r=fp.GetBoundingBox(False)
    if mm(r.GetY())<12: print(fp.GetReference(),mm(r.GetX()),mm(r.GetY()),mm(r.GetRight()),mm(r.GetBottom()), "bottom" if fp.IsFlipped() else "top")
for t in b.GetTracks():
    r=t.GetBoundingBox()
    if mm(r.GetY())<9 and mm(r.GetRight())>19.5:
        print(t.GetClass(),t.GetNetname(),b.GetLayerName(t.GetLayer()),round(mm(r.GetX()),2),round(mm(r.GetY()),2),round(mm(r.GetRight()),2),round(mm(r.GetBottom()),2))
for d in b.GetDrawings():
    r=d.GetBoundingBox()
    if mm(r.GetY())<9 and mm(r.GetRight())>19.5: print("draw",b.GetLayerName(d.GetLayer()),round(mm(r.GetX()),2),round(mm(r.GetY()),2),round(mm(r.GetRight()),2),round(mm(r.GetBottom()),2))
