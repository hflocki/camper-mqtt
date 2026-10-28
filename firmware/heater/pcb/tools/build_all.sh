#!/bin/bash
# Rebuilds the board from parts.py/place.py: place -> autoroute -> GND pours -> DRC -> manufacturing files.
# Needs KiCad 7 (pcbnew Python module, kicad-cli), Java 17+, xvfb-run and freerouting.jar (v1.9.0) in this folder.
set -e
cd "$(dirname "$0")"
mkdir -p build
python3 place.py build/place.kicad_pcb
python3 check.py build/place.kicad_pcb | grep -v "U1\|J1 \|J4 " || true
python3 route.py build/place.kicad_pcb build/routed.kicad_pcb 40 | tail -1
python3 finish.py build/routed.kicad_pcb build/final.kicad_pcb
python3 drc.py build/final.kicad_pcb
mkdir -p ../kicad ../fertigung ../docs
cp build/final.kicad_pcb ../kicad/camper-heater-lin.kicad_pcb
cp build/final.kicad_pro ../kicad/camper-heater-lin.kicad_pro
python3 make_outputs.py ../kicad/camper-heater-lin.kicad_pcb ../fertigung
python3 schematic.py ../docs/schaltplan
