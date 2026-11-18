"""Autoroute the board with Freerouting.

Strategy (2-layer, fine-pitch TQFP):
  * B.Cu keeps a solid GND pour during routing -> Freerouting treats GND as a plane net.
  * Freerouting's fanout stage escapes plane-net SMD pads to vias, so each GND pin reaches B.Cu.
  * F.Cu GND pour is removed for routing (it cannot reach 0.5 mm-pitch pins reliably) and restored
    afterwards as extra return path.
Usage: python.exe -I route.py <board.kicad_pcb> <java.exe> <freerouting.jar> [passes]
"""
import os, subprocess, sys
import pcbnew

pcb, java, jar = sys.argv[1:4]
passes = sys.argv[4] if len(sys.argv) > 4 else "60"
work = os.path.dirname(os.path.abspath(pcb))
dsn, ses, log = (os.path.join(work, n) for n in ("board.dsn", "board.ses", "fr.log"))

b = pcbnew.LoadBoard(pcb)
for t in list(b.GetTracks()):          # drop previous routing, keep locked pre-fanout (GND escapes)
    if not t.IsLocked():
        b.Remove(t)

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
assert pcbnew.ExportSpecctraDSN(b, dsn), "DSN export failed"

# Keep B.Cu an unbroken GND plane: route on F.Cu, use B.Cu only for short crossings (high trace cost).
BOTTOM_COST = os.environ.get("BOTTOM_COST", "25")
settings = f"""    (autoroute_settings
      (vias on) (via_costs 30) (plane_via_costs 5) (start_ripup_costs 100)
      (layer_rule F.Cu (active on) (preferred_direction horizontal)
        (preferred_direction_trace_costs 1.0) (against_preferred_direction_trace_costs 1.0))
      (layer_rule B.Cu (active on) (preferred_direction vertical)
        (preferred_direction_trace_costs {BOTTOM_COST}) (against_preferred_direction_trace_costs {BOTTOM_COST}))
    )
"""
t = open(dsn, encoding="utf-8").read()
i = t.index("(layer B.Cu", t.index("(structure"))      # insert right after the last layer definition
depth = 0
for j in range(i, len(t)):
    depth += {"(": 1, ")": -1}.get(t[j], 0)
    if depth == 0:
        break
j = t.index("\n", j) + 1
open(dsn, "w", encoding="utf-8").write(t[:j] + settings + t[j:])

if os.path.exists(ses):
    os.remove(ses)
cmd = [java, "-jar", jar, "-de", dsn, "-do", ses, "--gui.enabled=false",
       f"--router.autorouter.max_passes={passes}",
       "--router.fanout.enabled=false"]
with open(log, "w") as lf:
    rc = subprocess.call(cmd, stdout=lf, stderr=subprocess.STDOUT)
if rc != 0 or not os.path.exists(ses):
    sys.exit(f"freerouting failed (rc={rc}); see {log}")

assert pcbnew.ImportSpecctraSES(b, ses), "SES import failed"
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(pcb, b)

items = list(pcbnew.LoadBoard(pcb).GetTracks())   # re-load: the board's track list is stale after SES import
print("tracks", sum(i.GetClass() == "PCB_TRACK" for i in items), "vias", sum(i.GetClass() == "PCB_VIA" for i in items))
for line in open(log, encoding="utf-8", errors="replace"):
    if "DEBUG" in line:
        continue
    if any(k in line for k in ("Auto-routing stage completed", "Optimization stage completed", "anout", "Unrecognized", "Unknown", "nvalid")):
        print(line.split("]", 1)[-1].strip()[:240])
