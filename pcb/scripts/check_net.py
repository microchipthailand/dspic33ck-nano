import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS
t = open(sys.argv[1], encoding="utf-8").read()
got = {}
for blk in re.split(r'\(net\s*\(code', t[t.index("(nets"):])[1:]:
    name = re.search(r'\(name "([^"]*)"', blk).group(1).lstrip("/")
    for n in re.finditer(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk):
        got[(n.group(1), n.group(2))] = name
bad = 0; checked = 0
for ref, lib, val, fp, nets, pos, mpn in PARTS:
    if ref.startswith("#"): continue
    for pin, net in nets.items():
        g = got.get((ref, pin))
        exp = net if net is not None else None
        if net is None:
            ok = g is None or g.startswith("unconnected-")
        else:
            ok = g == exp
        checked += 1
        if not ok:
            bad += 1; print("MISMATCH", ref, pin, "expected", exp, "got", g)
print(f"checked {checked} pins, {bad} mismatches, {len(set(got.values()))} nets in netlist")
