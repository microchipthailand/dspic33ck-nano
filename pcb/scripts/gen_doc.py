"""Fill board_guide_template.html with pinout tables, BOM and embedded images -> board_guide.html.
Usage: python.exe -I gen_doc.py <project_dir>
"""
import base64, csv, datetime, html, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import DSPIC_PINS, POWER, LEFT, RIGHT

PROJ = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))

# net -> MCU pin number
net_pin = {}
for p in range(1, 49):
    n = POWER.get(p) or DSPIC_PINS[p].split("/")[-1]
    net_pin.setdefault(n, []).append(p)

SHARED = {"RB5": "ICSP PGD", "RB6": "ICSP PGC", "RC10": "UART TX (J5-2)", "RC11": "UART RX (J5-3)",
          "RC12": "USER button", "RC13": "USER LED", "MCLR": "Reset / ICSP VPP"}


def row(i, net):
    if net in ("+5V", "+3V3"):
        tag, label = "pwr", net.lstrip("+")
        mcu, func = "—", "5 V rail (after D1)" if net == "+5V" else "3.3 V rail (LDO output)"
    elif net == "GND":
        tag, label, mcu, func = "gnd", "GND", "—", "Ground"
    else:
        p = net_pin[net][0]
        label, mcu = net, str(p)
        func = DSPIC_PINS[p].replace("~{MCLR}", "MCLR").replace("/", " · ")
        tag = "prog" if net in SHARED else "sig"
        if net in SHARED:
            func += f" — <b>{SHARED[net]}</b>"
    return (f'<tr><td>{i}</td><td><span class="tag {tag}">{html.escape(label)}</span></td>'
            f'<td class="num">{mcu}</td><td>{func}</td></tr>')


rows_j2 = "\n  ".join(row(i + 1, n) for i, n in enumerate(LEFT))
rows_j3 = "\n  ".join(row(i + 1, n) for i, n in enumerate(RIGHT))

bom_rows = []
with open(os.path.join(PROJ, "bom.csv"), encoding="utf-8") as f:
    for r in csv.DictReader(f):
        pkg = r["Footprint"].split(":")[1]
        pkg = (pkg.replace("_2012Metric", " (2012 metric)").replace("_1608Metric", " (1608 metric)")
                  .replace("USB_C_Receptacle_GCT_USB4125-xx-x-0190_6P_TopMnt_Horizontal", "USB-C 6P horizontal")
                  .replace("PinHeader_", "Header ").replace("_P2.54mm_Vertical", " 2.54 mm"))
        bom_rows.append(f'<tr><td>{html.escape(r["Refs"])}</td><td class="num">{r["Qty"]}</td>'
                        f'<td>{html.escape(r["Value"])}</td><td>{html.escape(pkg)}</td><td>{html.escape(r["MPN"])}</td></tr>')


def img(name):
    with open(os.path.join(PROJ, name), "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


t = open(os.path.join(HERE, "board_guide_template.html"), encoding="utf-8").read()
for k, v in {"{{ROWS_J2}}": rows_j2, "{{ROWS_J3}}": rows_j3, "{{BOM_ROWS}}": "\n  ".join(bom_rows),
             "{{IMG_TOP}}": img("top3d.png"), "{{IMG_BOT}}": img("bot3d.png"),
             "{{IMG_TRK_TOP}}": img("tracks_top.png"), "{{IMG_TRK_BOT}}": img("tracks_bot.png"),
             "{{DATE}}": datetime.date.today().isoformat()}.items():
    assert k in t, k
    t = t.replace(k, v)
assert "{{" not in t
out = os.path.join(PROJ, "board_guide.html")
open(out, "w", encoding="utf-8").write(t)
print("wrote", out, f"{len(t) // 1024} KB")
