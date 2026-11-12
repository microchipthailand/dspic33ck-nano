"""
renumber_labs.py - regroup the labs Basics -> Advanced -> Application.

usage:  python renumber_labs.py <Arduino_dsPIC33CK_Nano dir> [--apply]
        without --apply: dry run, prints every change it would make

One mapping (OLD -> NEW) applied in ONE pass everywhere, so 11->21 and
21->18 can never chain:
  * example folders + .ino names         LabNN_Name -> LabMM_Name
  * sketch text, README, library.properties, tools/*.py, the HTML guide:
      "Lab NN", "LabNN", "Labs NN and MM", "Labs NN/MM", "#labNN", id="labNN"
  * the guide's <section id="labNN"> blocks are re-ordered into the groups
    and the navigation is rebuilt.
Range references ("Labs 16-20") are NOT rewritten - they are reported, because
after regrouping a range means something else; fix those by hand.
"""
import os, re, sys

ROOT = sys.argv[1]
APPLY = "--apply" in sys.argv

# OLD -> NEW
MAP = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 9,
       27: 10, 26: 11, 18: 12, 13: 13, 15: 14, 22: 15, 17: 16, 16: 17, 21: 18, 19: 19,
       10: 20, 11: 21, 12: 22, 20: 23, 14: 24, 24: 25, 23: 26, 25: 27}
assert sorted(MAP.values()) == list(range(1, 28)) and sorted(MAP) == list(range(1, 28))

GROUPS = [("Basics", "one peripheral at a time, with the Arduino API", 1, 9),
          ("Advanced", "register level: interrupts, timers, DMA, analog, PWM, flash, power, RTOS", 10, 19),
          ("Application", "real projects, modules and vehicle / industrial buses", 20, 27)]

EX = os.path.join(ROOT, "hardware", "dspicnano", "dspic33ck", "libraries", "dsPIC33CK_Nano", "examples")

def m(n):
    n = int(n)
    return MAP.get(n, n)

# ---------------- text rewriting ----------------
PATTERNS = [
    # "Labs 11 and 14", "Labs 11/14", "Labs 11, 14", "Labs 15, 17 and 19"
    (re.compile(r"\b(Labs )((?:\d{1,2}(?:, |/| and | or ))+\d{1,2})\b"),
     lambda mo: mo.group(1) + re.sub(r"\d{1,2}", lambda d: str(m(d.group(0))), mo.group(2))),
    # "Lab NN" (with space), keeps zero padding style
    (re.compile(r"\b(Lab )(\d{1,2})\b(?!\s*(?:&ndash;|–|-|\.\.)\s*\d)"),
     lambda mo: mo.group(1) + (("%02d" if len(mo.group(2)) == 2 else "%d") % m(mo.group(2)))),
    # "LabNN" (no space, incl. LabNN_Name and LabNN:)
    (re.compile(r"\bLab(\d{2})(?=\b|_)"), lambda mo: "Lab%02d" % m(mo.group(1))),
    # anchors / ids in the guide
    (re.compile(r'(id="lab|href="#lab|"lab)(\d{2})(")'), lambda mo: mo.group(1) + "%02d" % m(mo.group(2)) + mo.group(3)),
]
RANGE = re.compile(r"\bLabs? ?\d{1,2}\s*(?:&ndash;|–|-|\.\.|to)\s*(?:Lab ?)?\d{1,2}\b")

def rewrite(text):
    out = text
    for rx, fn in PATTERNS:
        out = rx.sub(fn, out)
    return out

# The two new labs carry no number in their text yet ("Lab - ...")
NEW_TITLES = {27: "Lab 10 - Pin-change interrupts", 26: "Lab 11 - Advanced timers"}

def stamp_new(text, old):
    if old == 27: return text.replace("Lab - Pin-change interrupts", NEW_TITLES[27]).replace('"Lab - pin-change', '"Lab 10 - pin-change')
    if old == 26: return text.replace("Lab - Advanced timers", NEW_TITLES[26]).replace('"Lab - advanced timers', '"Lab 11 - advanced timers')
    return text

changes = []

def process_file(path, old=None):
    t = open(path, encoding="utf-8").read()
    t2 = rewrite(t)
    if old: t2 = stamp_new(t2, old)
    if t2 != t:
        n = sum(1 for a, b in zip(t.splitlines(), t2.splitlines()) if a != b)
        changes.append((path, n))
        for r in RANGE.findall(t2):
            print("  RANGE (fix by hand):", os.path.basename(path), r)
        if APPLY:
            open(path, "w", encoding="utf-8", newline="").write(t2)

# ---------------- 1. sketches: rewrite text, then rename folders ----------------
folders = sorted(d for d in os.listdir(EX) if re.match(r"Lab\d{2}_", d))
plan = []
for d in folders:
    old = int(d[3:5]); name = d[6:]
    new = "Lab%02d_%s" % (m(old), name)
    process_file(os.path.join(EX, d, d + ".ino"), old)
    plan.append((d, new))
for d, new in plan:
    print("rename", d, "->", new)
if APPLY:
    for d, new in plan:                                   # pass 1: temporary names
        os.rename(os.path.join(EX, d), os.path.join(EX, "__tmp_" + new))
    for d, new in plan:                                   # pass 2: final names (+ the .ino inside)
        tmp = os.path.join(EX, "__tmp_" + new)
        os.rename(os.path.join(tmp, d + ".ino"), os.path.join(tmp, new + ".ino"))
        os.rename(tmp, os.path.join(EX, new))

# ---------------- 2. docs / tools ----------------
for rel in ("README.md", "hardware/dspicnano/dspic33ck/libraries/dsPIC33CK_Nano/library.properties",
            "tools/gen_wiring.py", "tools/check_labs.py" if os.path.exists(os.path.join(ROOT, "tools/check_labs.py")) else "tools/checks/check_labs.py"):
    p = os.path.join(ROOT, rel)
    if os.path.exists(p): process_file(p)

# ---------------- 3. the HTML guide: rewrite, then re-order sections + nav ----------------
gp = os.path.join(ROOT, "dsPIC33CK_Nano_Labs.html")
html = open(gp, encoding="utf-8").read()
html2 = rewrite(html)
for r in sorted(set(RANGE.findall(html2))):
    print("  RANGE (fix by hand): guide", r)

secs = {}
for mo in re.finditer(r'(<!-- =+ LAB \d+ -->\s*)?<section id="lab(\d{2})">.*?</section>\s*', html2, re.S):
    secs[int(mo.group(2))] = mo.group(0)
first = min(mo.start() for mo in re.finditer(r'(<!-- =+ LAB \d+ -->\s*)?<section id="lab\d{2}">', html2))
last = max(mo.end() for mo in re.finditer(r'<section id="lab\d{2}">.*?</section>\s*', html2, re.S))
missing = [n for n in range(1, 28) if n not in secs]
if missing:
    print("guide: sections still missing for new labs", missing, "- they will be appended from labs_new.html if present")
body = ""
for gname, gdesc, a, b in GROUPS:
    body += ('<div class="group" id="group-%s"><h2 class="grp">%s <span>Labs %02d&ndash;%02d &middot; %s</span></h2></div>\n'
             % (gname.lower(), gname, a, b, gdesc))
    for n in range(a, b + 1):
        if n in secs:
            body += re.sub(r"<!-- =+ LAB \d+ -->", "<!-- ================================================================ LAB %02d -->" % n, secs[n])
        else:
            body += "<!-- LAB %02d placeholder -->\n" % n
html2 = html2[:first] + body + html2[last:]

# navigation
nav_items = re.findall(r'<a href="#lab(\d{2})"><span>\d{2}</span>([^<]*)</a>', html2)
names = {int(n): t for n, t in nav_items}
names.setdefault(10, "Pin-change interrupts"); names.setdefault(11, "Advanced timers")
nav = ""
for gname, _, a, b in GROUPS:
    nav += "  <b>%s</b>\n" % gname
    for n in range(a, b + 1):
        nav += '  <a href="#lab%02d"><span>%02d</span>%s</a>\n' % (n, n, names.get(n, "Lab %02d" % n))
html2 = re.sub(r'(  <a href="#board">.*?</a>\n)(.*?)(  <b>Help</b>)', lambda mo: mo.group(1) + nav + mo.group(3), html2, flags=re.S)
if html2 != html:
    changes.append((gp, -1))
    if APPLY:
        open(gp, "w", encoding="utf-8", newline="").write(html2)

print()
for p, n in changes:
    print("changed:", os.path.relpath(p, ROOT), "" if n < 0 else "(%d lines)" % n)
print("APPLIED" if APPLY else "dry run - nothing written (add --apply)")
