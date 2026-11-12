#!/usr/bin/env bash
# Regenerate the whole project from design.py and verify it. Usage: ./build.sh
set -euo pipefail
K=${KICAD_DIR:-"$LOCALAPPDATA/Programs/KiCad/10.0"}   # override with KICAD_DIR=...
PY="$K/bin/python.exe -I"; CLI="$K/bin/kicad-cli.exe"
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-"$HERE/../kicad"}   # WARNING: regenerates an UNROUTED board over the routed one
$PY "$HERE/gen_sch.py" "$K/share/kicad" "$OUT"
ROOT=$(grep -oE '\(uuid [0-9a-f-]{36}' "$OUT/dspic33ck_nano.kicad_sch" | head -1 | cut -c7-)
$PY "$HERE/gen_pro.py" "$K/share/kicad/demos/stickhub/StickHub.kicad_pro" "$OUT/dspic33ck_nano.kicad_pro" "$ROOT"
$PY "$HERE/gen_pcb.py" "$K/share/kicad" "$OUT"
$PY "$HERE/gen_pro.py" "$K/share/kicad/demos/stickhub/StickHub.kicad_pro" "$OUT/dspic33ck_nano.kicad_pro" "$ROOT"
cd "$OUT"
"$CLI" sch erc --severity-all -o erc.rpt dspic33ck_nano.kicad_sch >/dev/null; grep "ERC messages" erc.rpt
"$CLI" sch export netlist --format kicadsexpr -o net.net dspic33ck_nano.kicad_sch >/dev/null
$PY "$HERE/check_net.py" net.net
"$CLI" pcb drc --severity-all --schematic-parity -o drc.rpt dspic33ck_nano.kicad_pcb >/dev/null
echo "DRC:"; grep -oE "^\[[a-z_]+\]" drc.rpt | sort | uniq -c
