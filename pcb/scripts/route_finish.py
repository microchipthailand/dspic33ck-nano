"""Second Freerouting pass: lock everything already routed, route GND as an ordinary net (no planes),
so every GND fragment and any leftover signal gets a real track.
Usage: python.exe -I route_finish.py <board.kicad_pcb> <java.exe> <freerouting.jar>
"""
import os, re, subprocess, sys
import pcbnew

pcb, java, jar = sys.argv[1:4]
work = os.path.dirname(os.path.abspath(pcb))
dsn, ses, log = (os.path.join(work, n) for n in ("finish.dsn", "finish.ses", "finish.log"))

b = pcbnew.LoadBoard(pcb)
for t in b.GetTracks():
    t.SetLocked(True)
zones = [z.Duplicate(False) for z in b.Zones()]
for z in list(b.Zones()):
    b.Remove(z)
assert pcbnew.ExportSpecctraDSN(b, dsn)

t = open(dsn, encoding="utf-8").read()
t = re.sub(r"\n\s*\(plane [^\n]*\)", "", t)   # no plane nets in this pass
# Router copies locked items as "fixed"; keep the B.Cu cost so new GND links prefer short hops.
i = t.index("(layer B.Cu", t.index("(structure")); d = 0
for j in range(i, len(t)):
    d += {"(": 1, ")": -1}.get(t[j], 0)
    if d == 0:
        break
j = t.index("\n", j) + 1
t = t[:j] + """    (autoroute_settings (vias on) (via_costs 20) (start_ripup_costs 100)
      (layer_rule F.Cu (active on) (preferred_direction horizontal) (preferred_direction_trace_costs 1.0) (against_preferred_direction_trace_costs 1.0))
      (layer_rule B.Cu (active on) (preferred_direction vertical) (preferred_direction_trace_costs 3) (against_preferred_direction_trace_costs 3)))
""" + t[j:]
open(dsn, "w", encoding="utf-8").write(t)

if os.path.exists(ses):
    os.remove(ses)
with open(log, "w") as lf:
    rc = subprocess.call([java, "-jar", jar, "-de", dsn, "-do", ses, "--gui.enabled=false",
                          "--router.autorouter.max_passes=80", "--router.fanout.enabled=false",
                          "--router.optimizer.enabled=false"], stdout=lf, stderr=subprocess.STDOUT)
if rc != 0 or not os.path.exists(ses):
    sys.exit(f"freerouting failed rc={rc}; see {log}")
assert pcbnew.ImportSpecctraSES(b, ses)
for z in zones:
    b.Add(z)
pcbnew.SaveBoard(pcb, b)
# pcbnew state is unreliable after SES import in-process; unlock + refill in a fresh interpreter
subprocess.check_call([sys.executable, "-I", "-c",
    "import pcbnew,sys; b=pcbnew.LoadBoard(sys.argv[1]); [t.SetLocked(False) for t in b.GetTracks()]; "
    "pcbnew.ZONE_FILLER(b).Fill(b.Zones()); pcbnew.SaveBoard(sys.argv[1], b)", pcb])
for line in open(log, encoding="utf-8", errors="replace"):
    if "Auto-routing stage completed" in line or ("unrouted connection" in line and "DEBUG" not in line):
        print(line.split("]", 1)[-1].strip()[:200])
