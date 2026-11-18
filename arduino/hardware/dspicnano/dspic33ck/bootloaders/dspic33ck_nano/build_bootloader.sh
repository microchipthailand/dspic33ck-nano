#!/bin/sh
# Rebuild bootloader-dspic33ck-nano-115200-v1.hex for the dsPIC33CK Nano board.
# usage: sh build_bootloader.sh   (from Git Bash; needs XC-DSC v4.00 and the MC DFP)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
X=${XCDSC:-"/c/Program Files/Microchip/xc-dsc/v4.00/bin"}
DFP=${DFP:-$(ls -d "$LOCALAPPDATA"/Arduino15/packages/microchip/tools/dsPIC33CK-MC_DFP/*/xc16 2>/dev/null | tail -1)}
[ -d "$DFP" ] || DFP=$(ls -d /c/Users/*/AppData/Local/Arduino15/packages/microchip/tools/dsPIC33CK-MC_DFP/*/xc16 | tail -1)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
"$X/xc-dsc-gcc.exe" -mcpu=33CK256MC005 -mdfp="$DFP" -Os -ffunction-sections -fdata-sections \
  "$HERE"/bl_main.c "$HERE"/bl_crc.c "$HERE"/bl_nvm.c "$HERE"/bl_time.c "$HERE"/bl_uart.c \
  -T "$HERE/../../ldscripts/p33CK256MC005-boot.gld" -Wl,--no-ivt,--gc-sections,-Map="$OUT/bl.map" -o "$OUT/bl.elf"
"$X/xc-dsc-bin2hex.exe" -mdfp="$DFP" "$OUT/bl.elf"
cp "$OUT/bl.hex" "$HERE/bootloader-dspic33ck-nano-115200-v1.hex"
[ -n "$BL_ELF_OUT" ] && cp "$OUT/bl.elf" "$BL_ELF_OUT"   # optional: keep the ELF for disassembly checks
echo "wrote $HERE/bootloader-dspic33ck-nano-115200-v1.hex"
