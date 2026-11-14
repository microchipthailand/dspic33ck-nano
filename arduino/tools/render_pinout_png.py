"""render (for the repo README image docs/images/pinout.png) the 'Pinout' section of dsPIC33CK_Nano_Pinout.html to a PNG.
usage: render_pinout_png.py <pinout.html> <out.png> [width height]   (default 1260 985)
   needs Google Chrome; set CHROME=<path to chrome> if it is not in the default place"""
import os, re, subprocess, sys
CHROME = os.environ.get("CHROME", r"C:\Program Files\Google\Chrome\Application\chrome.exe")
src, out = sys.argv[1], sys.argv[2]
w, h = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (1260, 985)
t = open(src, encoding="utf-8").read()
head = re.search(r"<head>.*?</head>", t, re.S).group(0)
i = t.index("<h2>Pinout")
a = t.rindex("<section", 0, i); b = t.index("</section>", i) + 10
body = t[a:b].replace("<h2>Pinout (top view, USB-C at the top)</h2>", "")
page = os.path.join(os.path.dirname(out), "pinout_render.html")
open(page, "w", encoding="utf-8").write("<!doctype html><html>" + head +
    '<body style="margin:0;padding:10px 12px;background:#fff">' + body + "</body></html>")
subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                "--window-size=%d,%d" % (w, h), "--screenshot=" + out, "file:///" + page.replace("\\", "/")],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
print(out, os.path.getsize(out))
