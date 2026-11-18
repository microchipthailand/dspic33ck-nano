dsPIC33CK Nano - PCB fabrication package
========================================
Exported 2026-11-11 with KiCad 10.0.7 (kicad-cli) from
pcb/kicad/dspic33ck_nano.kicad_pcb

UPLOAD THIS TO THE PCB FAB:  dspic33ck_nano_gerbers.zip
(same files as the gerber\ folder)

Board specification
-------------------
  Size              31.4 x 74.5 mm, 4 corners rounded R2.0 mm
  Layers            2 (F.Cu top, B.Cu bottom), GND pour on both sides
  Thickness         1.6 mm FR-4
  Copper            1 oz (35 um)
  Solder mask       RED, both sides
  Silkscreen        WHITE, both sides
  Surface finish    HASL lead-free (ENIG also fine)
  Min track/space   0.15 / 0.15 mm
  Min drill         0.3 mm (vias 0.3/0.6 mm and 0.4/0.8 mm)
  Holes             166 plated (58 pads + 108 vias), 0 non-plated
  Castellated / impedance / via-in-pad: none

Files in gerber\
----------------
  dspic33ck_nano-F_Cu.gtl          top copper
  dspic33ck_nano-B_Cu.gbl          bottom copper
  dspic33ck_nano-F_Mask.gts        top solder mask
  dspic33ck_nano-B_Mask.gbs        bottom solder mask
  dspic33ck_nano-F_Silkscreen.gto  top silkscreen (incl. Microchip logo)
  dspic33ck_nano-B_Silkscreen.gbo  bottom silkscreen
  dspic33ck_nano-F_Paste.gtp       top paste (stencil)
  dspic33ck_nano-B_Paste.gbp       bottom paste (empty - no SMD parts on bottom)
  dspic33ck_nano-Edge_Cuts.gm1     board outline
  dspic33ck_nano-PTH.drl           plated drill holes (Excellon, mm)
  dspic33ck_nano-NPTH.drl          non-plated holes (empty)
  *-drl_map.gbr                    drill maps (reference only)
  dspic33ck_nano-job.gbrjob        Gerber X2 job file (stackup, colours)

assembly\  (only for assembled boards, PCBA)
  dspic33ck_nano-bom.csv           bill of materials, grouped
  dspic33ck_nano-pos.csv           pick-and-place, mm, both sides
  NOTE: the MPN column still holds generic descriptions for passives
  (e.g. "0805 100nF 16V X7R"); map them to the assembler's part numbers.
  J2/J3 (2 x 22-pin) and J4/J5 headers are through-hole on the BOTTOM side
  and are normally hand-soldered.

docs\
  schematic PDF, top/bottom layer PDFs, 3D renders, DRC + ERC reports

Check results at export
-----------------------
  ERC  0 errors, 0 warnings
  DRC  0 violations, 0 unconnected pads (zones refilled before export)
  Schematic <-> PCB parity: 1 note only - J4 pin 6 is intentionally not
       connected (6-pin PICkit header, pin 6 unused)
  156/156 component pins carry the net defined in pcb/scripts/design.py
