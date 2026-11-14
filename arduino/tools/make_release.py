#!/usr/bin/env python3
"""Build the Boards Manager release of the dsPIC33CK Nano platform.

    python arduino/tools/make_release.py [--base-url URL]

* zips arduino/hardware/dspicnano/dspic33ck  ->  dist/dspicnano-dspic33ck-<ver>.zip
  (one top-level folder, as Boards Manager expects)
* writes package_dspicnano_index.json in the repo root with the new
  url / checksum / size (the version comes from platform.txt)

Then: create GitHub release  v<ver>, attach the zip, commit + push the index.
"""
import argparse, hashlib, json, os, re, zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLATFORM = REPO / "arduino" / "hardware" / "dspicnano" / "dspic33ck"
DIST = REPO / "dist"
INDEX = REPO / "package_dspicnano_index.json"
GH = "https://github.com/microchipthailand/dspic33ck-nano"

SKIP_DIRS = {"__pycache__", "build", "_build"}
SKIP_FILES = {"platform.local.txt", "Thumbs.db", ".DS_Store"}

# Device packs (pruned Microchip DFPs, Apache-2.0). Attach both zips to every
# GitHub release next to the platform zip (copies are kept in dist/).
TOOLS = [
    ("dsPIC33CK-MC_DFP", "1.11.412-pruned.1", 652421,
     "5533d058359bd7482401c8c2011a5dbd05fd4396271cad93bd4c8cc54a076651"),
    ("dsPIC33CK-MP_DFP", "1.16.521-pruned.1", 810615,
     "6cee9c30b3027b2dd14ccd64483440a5432613e3945a7d4a820795cfd0476899"),
]


def version():
    m = re.search(r"^version=(\S+)", (PLATFORM / "platform.txt").read_text(), re.M)
    return m.group(1)


def boards():
    txt = (PLATFORM / "boards.txt").read_text(encoding="utf-8")
    return [{"name": n} for n in re.findall(r"^\w+\.name=(.+)$", txt, re.M)]


def make_zip(path):
    files = []
    for root, dirs, names in os.walk(PLATFORM):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for n in sorted(names):
            if n in SKIP_FILES or n.endswith(".pyc"):
                continue
            files.append(Path(root) / n)
    path.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files:
            arc = "dspic33ck/" + f.relative_to(PLATFORM).as_posix()
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, f.read_bytes())
    return len(files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", help="override the download URL folder (for local tests)")
    a = ap.parse_args()

    ver = version()
    name = f"dspicnano-dspic33ck-{ver}.zip"
    zpath = DIST / name
    n = make_zip(zpath)
    data = zpath.read_bytes()
    base = a.base_url or f"{GH}/releases/download/v{ver}"

    index = {"packages": [{
        "name": "dspicnano",
        "maintainer": "microchipthailand (community project; not affiliated with Arduino SA or Microchip Technology)",
        "websiteURL": GH,
        "email": "microchipthailand45@gmail.com",
        "help": {"online": GH},
        "platforms": [{
            "name": "dsPIC33CK Nano (Windows only)",
            "architecture": "dspic33ck",
            "version": ver,
            "category": "Contributed",
            "help": {"online": GH},
            "url": f"{base}/{name}",
            "archiveFileName": name,
            "checksum": "SHA-256:" + hashlib.sha256(data).hexdigest(),
            "size": str(len(data)),
            "boards": boards(),
            "toolsDependencies": [
                {"packager": "dspicnano", "name": t, "version": v} for t, v, _, _ in TOOLS],
        }],
        "tools": [{
            "name": t, "version": v,
            "systems": [{
                "host": host,
                "url": f"{base}/{t}-{v}.zip",
                "archiveFileName": f"{t}-{v}.zip",
                "size": str(size),
                "checksum": "SHA-256:" + sha,
            } for host in ("i686-mingw32", "x86_64-mingw32")],
        } for t, v, size, sha in TOOLS],
    }]}
    if a.base_url:  # local test: do not touch the committed index
        out = DIST / INDEX.name
    else:
        out = INDEX
    out.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"{zpath.relative_to(REPO)}: {n} files, {len(data)} bytes")
    print(f"index -> {out.relative_to(REPO)}")


if __name__ == "__main__":
    main()
