"""Check every 'Rxn ... (J2-n)' / 'Rxn (J3-n)' reference in the labs against the PCB netlist."""
import sys, re, glob
import pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "pcb" / "scripts"))
from design import LEFT, RIGHT
hdr = {("J2", i + 1): n for i, n in enumerate(LEFT)}
hdr.update({("J3", i + 1): n for i, n in enumerate(RIGHT)})
bad = 0; n = 0
for f in glob.glob(sys.argv[1] + "/*/*.ino"):
    for ln_no, ln in enumerate(open(f, encoding="utf-8"), 1):
        for m in re.finditer(r"(R[A-D]\d{1,2}|3V3|GND)[^()\n]{0,25}?\((J[23])-(\d+)\)", ln):
            sig, j, p = m.group(1), m.group(2), int(m.group(3))
            want = hdr[(j, p)].lstrip("+")
            n += 1
            if want != sig:
                bad += 1; print(f"{f.split(chr(92))[-1]}:{ln_no}: {sig} says {j}-{p} but {j}-{p} is {want}")
print(f"checked {n} header references, {bad} wrong")
