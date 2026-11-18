"""Write the .kicad_pro with design rules + net classes (JLCPCB/PCBWay 2-layer standard capability).
Starts from a KiCad demo project's settings so every key KiCad expects is present.
Usage: python.exe -I gen_pro.py <template.kicad_pro> <out.kicad_pro> <root_sheet_uuid>
"""
import json, sys

tmpl, out, root = sys.argv[1:4]
d = json.load(open(tmpl, encoding="utf-8"))

r = d["board"]["design_settings"]["rules"]
r.update(min_clearance=0.15, min_track_width=0.15, min_via_diameter=0.5, min_through_hole_diameter=0.3,
         min_via_annular_width=0.1, min_copper_edge_clearance=0.3, min_hole_to_hole=0.5, min_hole_clearance=0.25,
         min_silk_clearance=0.0, min_text_height=0.6, min_text_thickness=0.1)
d["board"]["design_settings"]["drc_exclusions"] = []
d["board"]["design_settings"]["track_widths"] = [0.0, 0.2, 0.3, 0.4, 0.6]
d["board"]["design_settings"]["via_dimensions"] = [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}, {"diameter": 0.8, "drill": 0.4}]

base = dict(d["net_settings"]["classes"][0])
default = dict(base, name="Default", clearance=0.2, track_width=0.2, via_diameter=0.6, via_drill=0.3)
power = dict(base, name="Power", clearance=0.15, track_width=0.4, via_diameter=0.8, via_drill=0.4, priority=0)
d["net_settings"]["classes"] = [default, power]
d["net_settings"]["netclass_assignments"] = None
d["net_settings"]["netclass_patterns"] = [{"netclass": "Power", "pattern": "/" + n} for n in ("VBUS", "+5V", "+3V3", "+3V3A", "GND")]
d["net_settings"]["net_colors"] = None

d["meta"]["filename"] = out.replace("\\", "/").split("/")[-1]
d["sheets"] = [[root, "Root"]]
d["text_variables"] = {}
d.pop("boards", None)
d["libraries"] = {"pinned_footprint_libs": [], "pinned_symbol_libs": []}
d["pcbnew"]["last_paths"] = {k: "" for k in d["pcbnew"].get("last_paths", {})}
d["schematic"]["legacy_lib_dir"] = ""
json.dump(d, open(out, "w", encoding="utf-8"), indent=2)
print("project written", out)
