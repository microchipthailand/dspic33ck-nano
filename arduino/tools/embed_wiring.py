"""
embed_wiring.py - put the breadboard pictures from gen_wiring.py into the lab guide.

usage: python embed_wiring.py <dsPIC33CK_Nano_Labs.html> <wiring.json>

Inserts (or replaces, so it can be re-run) a <figure class="wiring"> in every
<section id="labNN"> right after the "You need" paragraph. Idempotent: an
existing figure between the markers is replaced.
"""
import json, re, sys

html_path, json_path = sys.argv[1:3]
html = open(html_path, encoding="utf-8").read()
svgs = json.load(open(json_path, encoding="utf-8"))

CSS = """figure.wiring { margin:10px 0 14px; padding:10px 12px 6px; background:#fbfaf7; border:1px solid var(--line); border-radius:10px; }
figure.wiring svg { display:block; width:100%; height:auto; }
figure.wiring figcaption { font-size:13px; color:var(--mute); margin-top:4px; }
figure.wiring figcaption b { color:var(--ink); }
"""
if "figure.wiring" not in html:
    html = html.replace("</style>", CSS + "</style>", 1)

START, END = "<!-- wiring:start -->", "<!-- wiring:end -->"
done = 0
for key, svg in sorted(svgs.items()):
    m = re.search(r'<section id="%s">(.*?)</section>' % key, html, re.S)
    if not m:
        raise SystemExit("section %s not found" % key)
    body = m.group(1)
    fig = (START + '<figure class="wiring">' + svg +
           '<figcaption><b>Wiring diagram.</b> Board seen from above, USB-C on the left, J2 along the top, '
           'J3 along the bottom. Each colour-coded wire is listed under the picture with its exact header pin. '
           'The pin numbers come from the PCB netlist.</figcaption></figure>' + END)
    if START in body:
        body2 = re.sub(re.escape(START) + ".*?" + re.escape(END), lambda _: fig, body, flags=re.S)
    else:
        # after the "You need" paragraph; fall back to after the tags line
        mm = re.search(r'(<h3>You need</h3>\s*<p>.*?</p>)', body, re.S)
        if mm:
            body2 = body[:mm.end()] + "\n" + fig + body[mm.end():]
        else:
            mm = re.search(r'(<div class="tags">.*?</div>)', body, re.S)
            body2 = body[:mm.end()] + "\n" + fig + body[mm.end():]
    html = html[:m.start(1)] + body2 + html[m.end(1):]
    done += 1

open(html_path, "w", encoding="utf-8", newline="").write(html)
print("embedded", done, "wiring diagrams")
