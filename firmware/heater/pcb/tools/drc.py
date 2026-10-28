import pcbnew, sys, re, collections
b = pcbnew.LoadBoard(sys.argv[1])
if '--fill' in sys.argv:
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
rep = sys.argv[1].replace('.kicad_pcb', '-drc.txt')
pcbnew.WriteDRCReport(b, rep, pcbnew.EDA_UNITS_MILLIMETRES, True)
txt = open(rep).read()
cnt = collections.Counter(re.findall(r'^\[(\w+)\]', txt, re.M))
print(dict(cnt))
print(re.search(r'\*\* Found \d+ unconnected pads \*\*', txt).group(0) if re.search(r'\*\* Found \d+ unconnected pads \*\*', txt) else '')
if '-v' in sys.argv: print(txt[:6000])
