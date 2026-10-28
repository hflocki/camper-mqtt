"""Autoroute a placed board with Freerouting and import the result.

usage: python3 route.py placed.kicad_pcb routed.kicad_pcb [passes]
GND is left out of the autorouter (copper pours on both layers do it).
"""
import pcbnew
import re
import shutil
import subprocess
import sys

src, dst = sys.argv[1], sys.argv[2]
passes = sys.argv[3] if len(sys.argv) > 3 else '20'
shutil.copy(src.replace('.kicad_pcb', '.kicad_pro'), dst.replace('.kicad_pcb', '.kicad_pro'))
b = pcbnew.LoadBoard(src)
dsn = dst.replace('.kicad_pcb', '.dsn')
ses = dst.replace('.kicad_pcb', '.ses')
pcbnew.ExportSpecctraDSN(b, dsn)
t = open(dsn).read()
if '--no-gnd' in sys.argv:
    t = re.sub(r'\(net GND\s*\(pins[^)]*\)\s*\)', '', t)
    t = re.sub(r'\n\s*\(via "[^"]+"\s+-?\d+\s+-?\d+\s+\(net GND\)\(type route\)\)', '', t)
open(dsn, 'w').write(t)
r = subprocess.run(['xvfb-run', '-a', 'java', '-jar', 'freerouting.jar', '-de', dsn, '-do', ses, '-mp', passes, '-da', '-mt', '1'],
                   capture_output=True, text=True, timeout=1500)
for line in (r.stdout + r.stderr).splitlines():
    if 'INFO' in line and ('routing' in line.lower() or 'optimiz' in line.lower()):
        print(line[24:])

# ---- import the session file (tracks and vias) ----
s = open(ses).read()
res = re.search(r'\(resolution\s+(\w+)\s+(\d+)\)', s)
scale = {'um': 1e-3, 'mm': 1.0, 'mil': 0.0254}[res.group(1)] / int(res.group(2))   # session units -> mm
layer = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
P = lambda x, y: pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
# the session contains every wire (also pre-routed ones): start from an empty board
for tr in list(b.GetTracks()):
    b.Remove(tr)
ntr = nvia = 0
net_out = s[s.find('(network_out'):]
for blk in re.split(r'\n\s*\(net ', net_out)[1:]:
    name = re.match(r'"?([^"\s)]+)"?', blk).group(1)
    ni = b.FindNet(name)
    if ni is None:
        print('unknown net', name)
        continue
    for w in re.finditer(r'\(path\s+(\S+)\s+(\d+)\s+([-\d\s]+)\)', blk):
        c = list(map(int, w.group(3).split()))
        pts = [(c[i] * scale, -c[i + 1] * scale) for i in range(0, len(c), 2)]
        # drop the tiny zig-zag segments the router leaves at pad entries
        clean = [pts[0]]
        for q in pts[1:]:
            if abs(q[0] - clean[-1][0]) + abs(q[1] - clean[-1][1]) > 0.02:
                clean.append(q)
        if len(clean) == 1:
            continue
        pts = clean
        for a, e in zip(pts, pts[1:]):
            tr = pcbnew.PCB_TRACK(b)
            tr.SetLayer(layer[w.group(1)])
            tr.SetWidth(pcbnew.FromMM(int(w.group(2)) * scale))
            tr.SetNet(ni)
            tr.SetStart(P(*a)); tr.SetEnd(P(*e))
            b.Add(tr); ntr += 1
    for v in re.finditer(r'\(via\s+"?([^"\s)]+)"?\s+(-?\d+)\s+(-?\d+)', blk):
        m = re.search(r'(\d+):(\d+)_um', v.group(1))
        dia, drill = (int(m.group(1)) / 1000, int(m.group(2)) / 1000) if m else (0.6, 0.3)
        via = pcbnew.PCB_VIA(b)
        via.SetPosition(P(int(v.group(2)) * scale, -int(v.group(3)) * scale))
        via.SetWidth(pcbnew.FromMM(dia)); via.SetDrill(pcbnew.FromMM(drill)); via.SetNet(ni)
        b.Add(via); nvia += 1
print('imported tracks', ntr, 'vias', nvia)
pcbnew.SaveBoard(dst, b)
