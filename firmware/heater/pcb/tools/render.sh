#!/bin/bash
# render.sh board.kicad_pcb out.png layers
d=$(dirname "$0")
kicad-cli pcb export svg --layers "$3" --exclude-drawing-sheet --page-size-mode 2 -o /tmp/_r.svg "$1" >/dev/null 2>&1
node "$d/render.js" /tmp/_r.svg "$2" ${4:-1400}
