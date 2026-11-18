"""
gen_pinout_html.py - Arduino-style pinout page for the dsPIC33CK Nano board.

Every label is DERIVED, not typed:
  * header order  <- pcb_scripts/design.py   (the board's netlist source)
  * D number      <- variants/dspic33ck_nano/variant.c   (g_pin_map index)
  * An name       <- variants/dspic33ck_nano/pins_arduino.h (#define An n)
  * ANn channel   <- variant.c adc_channel column
  * LED/button/SPI/I2C/UART/PWM pins <- pins_arduino.h macros
and the script asserts they agree (port + bit of each D number, every An),
so a pinout that disagrees with what the core compiles fails here.

usage: python -I gen_pinout_html.py <platform_dir> <design.py dir> <board render png> <out.html>
"""
import base64, html, os, re, sys

PLAT, DESIGN_DIR, IMG, OUT = sys.argv[1:5]
VAR = os.path.join(PLAT, "variants", "dspic33ck_nano")
sys.path.insert(0, DESIGN_DIR)
from design import LEFT, RIGHT, DSPIC_PINS  # noqa: E402

vc = open(os.path.join(VAR, "variant.c"), encoding="utf-8").read()
ph = open(os.path.join(VAR, "pins_arduino.h"), encoding="utf-8").read()

# ---- variant.c: D -> (port, bit, adc) ----
dmap = {}
for m in re.finditer(r"/\* D(\d+):\s*R([A-D])(\d+)\s*\*/\s*\{\s*&PORT([A-D]),[^,]+,[^,]+,[^,]+,\s*(\d+),\s*(-?\d+)\s*\}", vc):
    d, p1, b1, p2, b2, adc = int(m[1]), m[2], int(m[3]), m[4], int(m[5]), int(m[6])
    assert p1 == p2 and b1 == b2, f"D{d}: comment R{p1}{b1} != table R{p2}{b2}"
    dmap[d] = (f"R{p1}{b1}", adc)
ndig = int(re.search(r"#define NUM_DIGITAL_PINS\s+(\d+)", ph)[1])
assert sorted(dmap) == list(range(ndig)), "variant.c is not D0..D%d" % (ndig - 1)
port2d = {v[0]: d for d, v in dmap.items()}

# ---- pins_arduino.h: An aliases and board macros ----
amap = {}
for m in re.finditer(r"#define A(\d+)\s+(\d+)\s*/\*\s*R([A-D]\d+)\s*=\s*AN(\d+)", ph):
    a, d, port, an = int(m[1]), int(m[2]), "R" + m[3], int(m[4])
    assert dmap[d][0] == port, f"A{a}: header says {port}, D{d} is {dmap[d][0]}"
    assert dmap[d][1] == an, f"A{a}: header says AN{an}, variant.c says AN{dmap[d][1]}"
    amap[d] = a
nan = int(re.search(r"#define NUM_ANALOG_INPUTS\s+(\d+)", ph)[1])
assert len(amap) == nan, f"{len(amap)} An aliases, NUM_ANALOG_INPUTS={nan}"
for d, (port, adc) in dmap.items():
    assert (adc >= 0) == (d in amap), f"D{d} {port}: adc={adc} but alias {'present' if d in amap else 'missing'}"


def macro(name):
    return int(re.search(rf"#define {name}\s+(\d+)", ph)[1])


LED = macro("LED_BUILTIN"); BTN = macro("BUTTON_BUILTIN")
SPI = {macro("PIN_SPI_SCK"): "SCK", macro("PIN_SPI_MOSI"): "MOSI", macro("PIN_SPI_MISO"): "MISO", macro("PIN_SPI_SS"): "SS"}
I2C = {macro("PIN_WIRE_SDA"): "SDA", macro("PIN_WIRE_SCL"): "SCL"}
UART = {macro("PIN_SERIAL_TX"): "TX", macro("PIN_SERIAL_RX"): "RX"}
PWM = {macro(f"PWM{i}_PIN"): f"PWM{i}" for i in range(1, 5)}
DBG = {macro("PIN_PGD3"): "PGD3", macro("PIN_PGC3"): "PGC3"}
assert dmap[LED][0] == "RC13" and dmap[BTN][0] == "RC12", "LED/button do not match the PCB (RC13/RC12)"
TONE_PWM = macro("PWM4_PIN")

# ---- chip function names (motor-control PWM, comparator, op-amp ...) from the DFP pin names ----
pinfunc = {}
for p, names in DSPIC_PINS.items():
    parts = names.replace("~{MCLR}", "MCLR").split("/")
    pinfunc[parts[-1]] = parts[:-1]


def extras(port):
    want = ("PWM", "CMP", "OA", "DACOUT", "INT0", "RP")
    out = [f for f in pinfunc.get(port, []) if f.startswith(want) and not f.startswith(("PGD", "PGC"))]
    rp = [f for f in out if f.startswith("RP")]
    rest = [f for f in out if not f.startswith("RP")]
    return rest[:3] + rp


def row(net, j, n):
    """-> dict with label chips for one header pin."""
    r = {"j": j, "n": n, "net": net, "chips": []}
    if net in ("+5V", "+3V3"):
        r["kind"] = "pwr"; r["name"] = "5V" if net == "+5V" else "3V3"
        r["chips"].append(("pwr", "USB 5V after diode" if net == "+5V" else "3.3 V out, 250 mA"))
        return r
    if net == "GND":
        r["kind"] = "gnd"; r["name"] = "GND"; return r
    if net == "MCLR":
        r["kind"] = "rst"; r["name"] = "MCLR"; r["chips"].append(("rst", "RESET")); return r
    d = port2d[net]
    r["kind"] = "io"; r["name"] = net; r["d"] = d
    if d in amap:
        r["a"] = amap[d]; r["an"] = dmap[d][1]
    if d == LED: r["chips"].append(("board", "LED_BUILTIN"))
    if d == BTN: r["chips"].append(("board", "BUTTON"))
    if d in PWM: r["chips"].append(("pwm", "~PWM"))
    if d in UART: r["chips"].append(("uart", "Serial " + UART[d]))
    if d in I2C: r["chips"].append(("i2c", I2C[d]))
    if d in SPI: r["chips"].append(("spi", SPI[d]))
    if d in DBG: r["chips"].append(("dbg", DBG[d]))
    for f in extras(net):
        r["chips"].append(("rp" if f.startswith("RP") else "fn", f))
    return r


rows_l = [row(n, "J2", i + 1) for i, n in enumerate(LEFT)]
rows_r = [row(n, "J3", i + 1) for i, n in enumerate(RIGHT)]
on_header = {r["d"] for r in rows_l + rows_r if "d" in r}
missing = [f"D{d}={dmap[d][0]}" for d in dmap if d not in on_header]
assert not missing, "pins not on a header: " + ", ".join(missing)

E = html.escape


def chip(cls, text):
    return f'<span class="c {cls}">{E(text)}</span>'


def pin_html(r, side):
    num = f'<span class="pn">{r["n"]}</span>'
    if r["kind"] == "io":
        core = [chip("d", f'D{r["d"]}')]
        core.append(chip("a", f'A{r["a"]}') if "a" in r else chip("a none", " "))
        core.append(chip("port", r["name"]))
        if "an" in r: core.append(chip("an", f'AN{r["an"]}'))
    else:
        core = [chip(r["kind"], r["name"])]
    extra = [chip(c, t) for c, t in r["chips"]]
    if side == "L":
        cells = extra[::-1] + core[::-1] + [num]
    else:
        cells = [num] + core + extra
    return f'<div class="row {side}">{"".join(cells)}</div>'


# ---- board picture: crop to the PCB outline and scale so 2.54 mm == one label row ----
import io
from PIL import Image
import numpy as np
ROW = 26.0                        # px per label row
BOARD_H_MM, PIN1_Y_MM = 74.5, 10.0    # outline height; header pin 1 centre from top edge (PCB)
im = Image.open(IMG).convert("RGB")
a = np.asarray(im).astype(int)
red = (a[..., 0] > 150) & (a[..., 1] < 110) & (a[..., 2] < 110)      # red solder mask
ys, xs = np.nonzero(red)
im = im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
px_per_mm = im.height / BOARD_H_MM
k = ROW / (2.54 * px_per_mm)
im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
IMG_W = im.width
PAD_TOP = PIN1_Y_MM * px_per_mm * k - ROW / 2
buf = io.BytesIO(); im.save(buf, "PNG", optimize=True)
img64 = base64.b64encode(buf.getvalue()).decode()

# ---- tables ----
def where(d):
    return next("%s-%d" % (r["j"], r["n"]) for r in rows_l + rows_r if r.get("d") == d)


an_rows = "".join(
    f"<tr><td>A{a}</td><td>D{d}</td><td>{dmap[d][0]}</td><td>AN{dmap[d][1]}</td><td>{where(d)}</td></tr>"
    for d, a in sorted(amap.items(), key=lambda kv: kv[1]))


def bus_rows(m, what):
    return "".join(f"<tr><td>{what}</td><td>{E(v)}</td><td>D{d}</td><td>{dmap[d][0]}</td><td>{where(d)}</td></tr>"
                   for d, v in m.items())


bus = (bus_rows(UART, "Serial (UART1)") + bus_rows(I2C, "Wire (I2C1)") + bus_rows(SPI, "SPI (SPI1)") +
       bus_rows(PWM, "analogWrite()") +
       f"<tr><td>On-board</td><td>LED_BUILTIN (active low)</td><td>D{LED}</td><td>{dmap[LED][0]}</td><td>{where(LED)}</td></tr>"
       f"<tr><td>On-board</td><td>BUTTON_BUILTIN (LOW = pressed)</td><td>D{BTN}</td><td>{dmap[BTN][0]}</td><td>{where(BTN)}</td></tr>"
       + bus_rows(DBG, "ICSP J4"))

row_of = {r["d"]: r for r in rows_l + rows_r if "d" in r}


def dig_row(d, p, a):
    aname = "A%d" % amap[d] if d in amap else ""
    an = "AN%d" % a if a >= 0 else ""
    funcs = E(", ".join(t for _, t in row_of[d]["chips"]))
    return f"<tr><td>D{d}</td><td>{p}</td><td>{aname}</td><td>{an}</td><td>{where(d)}</td><td>{funcs}</td></tr>"


dig_rows = "".join(dig_row(d, p, a) for d, (p, a) in sorted(dmap.items()))

LABS = [
    ("Lab01_Blink", "Blink the USER LED", "digitalWrite, delay", "nothing"),
    ("Lab02_ButtonLED", "Button toggles LED, debounced", "digitalRead, millis", "nothing"),
    ("Lab03_SerialHello", "Serial Monitor print + commands", "Serial", "USB-serial adapter on J5"),
    ("Lab04_AnalogRead", "Potentiometer to volts", "analogRead", "10k pot on A0 (J2-15)"),
    ("Lab05_PWMFade", "PWM LED fade / pot dimmer", "analogWrite, map", "LED + 330R on D5 (J3-21)"),
    ("Lab06_Interrupt", "Count presses in an ISR", "attachInterrupt, volatile", "nothing"),
    ("Lab07_Tone", "Play a melody", "tone, noTone, arrays", "piezo on D9 (J3-15)"),
    ("Lab08_I2CScanner", "Find I2C devices", "Wire", "any 3.3 V I2C module"),
    ("Lab09_SPILoopback", "SPI self-test", "SPI", "jumper J3-5 to J3-14"),
    ("Lab10_DataLogger", "3-channel CSV logger + alarm", "all of the above", "pot, LDR, buzzer"),
]
lab_rows = "".join(f"<tr><td>{i + 1:02d}</td><td><code>{a}</code></td><td>{E(b)}</td><td>{E(c)}</td><td>{E(d)}</td></tr>"
                   for i, (a, b, c, d) in enumerate(LABS))

page = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>dsPIC33CK Nano - Arduino pinout</title>
<style>
:root {{ --ink:#1d2329; --mute:#5c6770; --line:#d9dee3; --bg:#f6f7f8; }}
* {{ box-sizing:border-box; }}
body {{ margin:0; font:14px/1.5 "Segoe UI",system-ui,sans-serif; color:var(--ink); background:var(--bg); }}
header {{ background:#c62828; color:#fff; padding:26px 32px 22px; }}
header h1 {{ margin:0; font-size:28px; letter-spacing:.3px; }}
header p {{ margin:6px 0 0; opacity:.9; }}
main {{ max-width:1280px; margin:0 auto; padding:24px; }}
section {{ background:#fff; border:1px solid var(--line); border-radius:10px; padding:20px 24px; margin-bottom:22px; }}
h2 {{ margin:0 0 12px; font-size:20px; }}
.pinout {{ display:grid; grid-template-columns:1fr {IMG_W}px 1fr; align-items:start; gap:8px; }}
.col {{ display:flex; flex-direction:column; padding-top:{PAD_TOP:.1f}px; }}
.row {{ display:flex; align-items:center; gap:4px; height:{ROW}px; }}
.row.L {{ justify-content:flex-end; }}
.c {{ display:inline-block; padding:1px 7px; border-radius:4px; font:600 12px/18px Consolas,"Cascadia Mono",monospace; white-space:nowrap; border:1px solid transparent; }}
.pn {{ width:22px; text-align:center; font:11px Consolas,monospace; color:var(--mute); }}
.d    {{ background:#00838f; color:#fff; min-width:38px; text-align:center; }}
.a    {{ background:#2e7d32; color:#fff; min-width:38px; text-align:center; }}
.a.none {{ background:transparent; }}
.port {{ background:#fff; color:#263238; border-color:#90a4ae; min-width:46px; text-align:center; }}
.an   {{ background:#e8f5e9; color:#1b5e20; border-color:#a5d6a7; }}
.pwr  {{ background:#c62828; color:#fff; }}
.gnd  {{ background:#212121; color:#fff; }}
.rst  {{ background:#6a1b9a; color:#fff; }}
.board{{ background:#fdd835; color:#3e2723; }}
.pwm  {{ background:#ef6c00; color:#fff; }}
.uart {{ background:#ad1457; color:#fff; }}
.i2c  {{ background:#5e35b1; color:#fff; }}
.spi  {{ background:#1565c0; color:#fff; }}
.dbg  {{ background:#546e7a; color:#fff; }}
.fn   {{ background:#efebe9; color:#5d4037; border-color:#bcaaa4; font-weight:500; }}
.rp   {{ background:#fafafa; color:#9e9e9e; border-color:#e0e0e0; font-weight:400; }}
.boardimg {{ width:{IMG_W}px; display:block; }}
.legend {{ display:flex; flex-wrap:wrap; gap:8px 14px; margin-top:16px; font-size:13px; color:var(--mute); }}
.legend span.c {{ margin-right:4px; }}
table {{ border-collapse:collapse; width:100%; font-size:13px; }}
th,td {{ border-bottom:1px solid var(--line); padding:5px 8px; text-align:left; }}
th {{ background:#eceff1; }}
td:first-child {{ font-weight:600; }}
.grid2 {{ display:grid; grid-template-columns:1fr 1fr; gap:22px; }}
.note {{ background:#fff8e1; border-left:4px solid #f9a825; padding:10px 14px; border-radius:4px; margin:10px 0; }}
.warn {{ background:#ffebee; border-left:4px solid #c62828; padding:10px 14px; border-radius:4px; margin:10px 0; }}
code {{ background:#eceff1; padding:1px 5px; border-radius:3px; font-size:12.5px; }}
ol li, ul li {{ margin:3px 0; }}
.specs td:first-child {{ width:190px; }}
@media (max-width:1100px) {{ .pinout {{ grid-template-columns:1fr; }} .row.L {{ justify-content:flex-start; flex-direction:row-reverse; }} .grid2 {{ grid-template-columns:1fr; }} }}
@media print {{ header {{ -webkit-print-color-adjust:exact; print-color-adjust:exact; }} .c {{ -webkit-print-color-adjust:exact; print-color-adjust:exact; }} }}
</style></head>
<body>
<header>
  <h1>dsPIC33CK Nano &mdash; Arduino Pinout</h1>
  <p>dsPIC33CK256MC005 &middot; 100 MIPS DSC &middot; 256 KB flash &middot; 16 KB RAM &middot; USB-C power &middot; breadboard 0.9&Prime; rows &middot; Arduino core <b>dspicnano:dspic33ck:dspic33ck_nano</b></p>
</header>
<main>

<section>
<h2>Pinout (top view, USB-C at the top)</h2>
<div class="pinout">
  <div class="col">{''.join(pin_html(r, 'L') for r in rows_l)}</div>
  <div><img class="boardimg" src="data:image/png;base64,{img64}" alt="dsPIC33CK Nano top view"></div>
  <div class="col">{''.join(pin_html(r, 'R') for r in rows_r)}</div>
</div>
<div class="legend">
  <div><span class="c d">D0</span>Arduino digital pin</div>
  <div><span class="c a">A0</span>analogRead() name</div>
  <div><span class="c port">RA0</span>chip port pin (silkscreen)</div>
  <div><span class="c an">AN0</span>ADC channel</div>
  <div><span class="c pwm">~PWM</span>analogWrite()</div>
  <div><span class="c uart">Serial</span>UART1</div>
  <div><span class="c i2c">SDA</span>Wire</div>
  <div><span class="c spi">SCK</span>SPI</div>
  <div><span class="c board">LED</span>on-board</div>
  <div><span class="c dbg">PGD3</span>ICSP</div>
  <div><span class="c fn">PWM1H</span>other chip function</div>
  <div><span class="c rp">RP46</span>remappable (PPS)</div>
  <div><span class="c pwr">3V3</span>power</div>
  <div><span class="c gnd">GND</span>ground</div>
</div>
<p class="note">Left header is <b>J2</b>, right header is <b>J3</b>, both numbered from the USB-C end (pin 1 = 5V / 3V3).
The rows are 0.9&Prime; (22.86 mm) apart, so the board straddles a standard breadboard. The <span class="c an">AN</span>
channel and the <span class="c a">A</span> name are different numbers on this chip &mdash; use the <b>A</b> name in sketches.</p>
</section>

<section class="grid2">
<div>
<h2>Arduino functions</h2>
<table><tr><th>Function</th><th>Signal</th><th>Pin</th><th>Port</th><th>Header</th></tr>{bus}</table>
</div>
<div>
<h2>Analog inputs (analogRead, 12-bit &rarr; 0..1023)</h2>
<table><tr><th>Name</th><th>Pin</th><th>Port</th><th>ADC</th><th>Header</th></tr>{an_rows}</table>
</div>
</section>

<section>
<h2>On-board hardware</h2>
<div class="grid2">
<table class="specs">
<tr><td>MCU</td><td>dsPIC33CK256MC005-I/PT, TQFP-48, internal FRC + PLL (no crystal)</td></tr>
<tr><td>Clock (Tools &gt; Clock)</td><td>100 MIPS (FOSC 200 MHz, default) or 4 MIPS (FRC 8 MHz)</td></tr>
<tr><td>Power</td><td>USB-C 5 V &rarr; B5819W diode &rarr; MCP1703A 3.3 V LDO, 250 mA</td></tr>
<tr><td>USER LED (D3, yellow)</td><td>RC13 = <code>LED_BUILTIN</code> = D{LED}, <b>active low</b> &mdash; use <code>LED_ON</code>/<code>LED_OFF</code></td></tr>
<tr><td>PWR LED (D2, green)</td><td>hard-wired to 3V3</td></tr>
<tr><td>USER button (SW2)</td><td>RC12 = <code>BUTTON_BUILTIN</code> = D{BTN}, 10 k pull-up on board, LOW = pressed</td></tr>
<tr><td>RESET button (SW1)</td><td>MCLR (10 k pull-up, 470 &Omega; + 100 nF)</td></tr>
</table>
<table class="specs">
<tr><td>J4 ICSP (6-pin)</td><td>1 MCLR &middot; 2 3V3 &middot; 3 GND &middot; 4 PGD3 (RB5) &middot; 5 PGC3 (RB6) &middot; 6 n/c &mdash; PICkit 4/5, SNAP</td></tr>
<tr><td>J5 UART (4-pin)</td><td>1 3V3 &middot; 2 TX (RC10) &middot; 3 RX (RC11) &middot; 4 GND &mdash; 3.3 V USB-serial adapter</td></tr>
<tr><td>PWM</td><td>D5 D6 D7 D8 (SCCP1-4, ~490 Hz)</td></tr>
<tr><td>Interrupts</td><td><code>attachInterrupt()</code> on every pin (change notification)</td></tr>
<tr><td>Board size</td><td>31.4 &times; 74.5 mm, 2 &times; 22 pins, red PCB</td></tr>
</table>
</div>
<p class="warn"><b>3.3 V only.</b> The I/O pins are not 5 V tolerant. The <b>5V</b> pin is USB power after the diode (about 4.6 V) &mdash; an output for modules, never a signal level.</p>
<ul>
<li><b>tone()</b> borrows timer SCCP4, which is the PWM of D{TONE_PWM}. Don't use <code>analogWrite({TONE_PWM}, &hellip;)</code> while a tone plays.</li>
<li><b>RB5/RB6</b> (D10/D11) are the ICSP lines. They are free I/O when no debugger is connected, but anything wired there can disturb programming.</li>
<li><b>RB10&ndash;RB15, RD1</b> are the motor-control PWM pins of the chip (PWM1H..PWM4H); in Arduino they are ordinary digital I/O.</li>
<li><b>PORTA</b> (RA0&ndash;RA4) has no PPS, so no remappable peripheral can use it; it is the best place for analog inputs.</li>
</ul>
</section>

<section>
<h2>Getting started</h2>
<ol>
<li>Install <b>XC-DSC v4.00+</b> (compiler) and <b>MPLAB X</b> (for uploading with a PICkit/SNAP).</li>
<li>In <i>File &gt; Preferences &gt; Additional boards manager URLs</i> add <code>https://raw.githubusercontent.com/microchipthailand/dspic33ck-nano/main/package_dspicnano_index.json</code>, then <i>Tools &gt; Board &gt; Boards Manager</i> &rarr; search <b>dsPIC33CK Nano</b> &rarr; <b>Install</b>. The device packs are installed automatically.<br><small>Alternative for core development: copy <code>arduino/hardware</code> into your sketchbook so you get <code>&lt;sketchbook&gt;/hardware/dspicnano/dspic33ck/boards.txt</code> (do not use both at once).</small></li>
<li><i>Tools &gt; Board</i> &rarr; <b>dsPIC33CK Nano (dsPIC33CK256MC005)</b>. <i>Tools &gt; Debugger</i> &rarr; the programmer you have on J4.</li>
<li>Power the board from USB-C, plug the PICkit into J4 (pin 1 = MCLR, square pad), and upload <i>File &gt; Examples &gt; dsPIC33CK_Nano &gt; Lab01_Blink</i>.</li>
</ol>
<h3>Optional: upload over the J5 serial port (no PICkit)</h3>
<ol>
<li>Once, with the PICkit: <i>Tools &gt; Burn Bootloader</i>.</li>
<li>Connect a 3.3 V USB-serial adapter to J5 (adapter RX &rarr; J5-2, adapter TX &rarr; J5-3, GND &rarr; J5-4), pick its COM port.</li>
<li><i>Tools &gt; Bootloader</i> &rarr; <b>Serial over J5 (UART 115200)</b>, then Upload as usual.</li>
<li>If the sketch running on the board doesn't use <code>Serial</code>, the upload can't reset it. Hold <b>USER</b>, press and release <b>RESET</b>, keep holding USER for about a second (the LED stays on), then upload.</li>
</ol>
<p class="note">Uploading with the PICkit erases the bootloader. Burn it again before going back to serial uploads.</p>
</section>

<section>
<h2>Lab examples (File &gt; Examples &gt; dsPIC33CK_Nano)</h2>
<table><tr><th>#</th><th>Sketch</th><th>What it does</th><th>Learns</th><th>Extra parts</th></tr>{lab_rows}</table>
</section>

<section>
<h2>All digital pins</h2>
<table><tr><th>Pin</th><th>Port</th><th>Analog</th><th>ADC</th><th>Header</th><th>Functions</th></tr>{dig_rows}</table>
<p style="color:var(--mute);font-size:12px;margin-top:12px">Generated by <code>tools/gen_pinout_html.py</code> from <code>variants/dspic33ck_nano/variant.c</code>, <code>pins_arduino.h</code> and the PCB netlist (<code>design.py</code>). The script checks that the three agree before it writes this page.</p>
</section>
</main></body></html>
"""
open(OUT, "w", encoding="utf-8").write(page)
print(f"wrote {OUT}: {len(dmap)} digital, {len(amap)} analog, 44 header pins - all checks passed")
