import pcbnew, sys
from parts import PARTS, L, POWER_NETS
mm = pcbnew.FromMM
OX, OY = 100.0, 100.0          # board origin in KiCad coordinates
W, H = 75.0, 66.0

# ref: (x, y, rotation, side) in board mm, origin top left
POS = {
 'U1': (26.0, 18.6, 0, 'F'),
 'J4': (71.85, 18.0, 90, 'F'), 'U4': (60.0, 18.0, 0, 'F'), 'R9': (64.0, 10.5, 90, 'F'), 'R10': (66.0, 10.5, 90, 'F'),
 'SW1': (6.5, 15.0, 90, 'F'), 'SW2': (6.5, 23.0, 90, 'F'),
 'C8': (9.95, 12.6, 180, 'F'), 'C7': (9.95, 10.4, 180, 'F'),
 'R1': (14.4, 13.35, 270, 'F'), 'C9': (13.0, 15.6, 180, 'F'),
 'R2': (11.6, 23.1, 0, 'F'), 'R3': (11.6, 21.3, 0, 'F'), 'R4': (38.0, 15.6, 90, 'F'),
 'LED1': (44.0, 10.0, 0, 'F'), 'R11': (44.0, 12.5, 0, 'F'), 'LED2': (48.0, 10.0, 0, 'F'), 'R12': (48.0, 12.5, 0, 'F'),
 'LED3': (52.0, 10.0, 0, 'F'), 'R13': (52.0, 12.5, 0, 'F'),
 'U3': (48.0, 22.0, 0, 'F'), 'C4': (44.5, 22.0, 90, 'F'), 'C5': (51.5, 22.0, 90, 'F'), 'C6': (48.0, 18.0, 0, 'F'),
 'D4': (58.0, 24.5, 0, 'F'), 'D3': (52.0, 26.8, 0, 'F'), 'TP2': (44.0, 26.5, 0, 'F'), 'TP3': (39.5, 21.0, 0, 'F'),
 'J1': (4.6, 38.0, 270, 'F'), 'F1': (13.5, 42.5, 90, 'F'),
 'D1': (22.5, 33.0, 0, 'F'), 'D2': (30.5, 33.0, 0, 'F'), 'C1': (36.5, 34.5, 90, 'F'), 'C2': (39.0, 34.5, 90, 'F'),
 'TP1': (42.5, 36.5, 0, 'F'), 'PS1': (50.0, 32.0, 0, 'F'), 'C3': (60.5, 32.0, 90, 'F'), 'TP4': (64.0, 29.5, 0, 'F'),
 'J2': (12.0, 48.6, 0, 'F'), 'J3': (52.0, 48.6, 0, 'F'),
 'U2': (35.0, 51.0, 0, 'F'),
 'R5': (28.5, 49.1, 0, 'F'), 'R7': (28.5, 50.95, 0, 'F'), 'R6': (28.5, 52.8, 0, 'F'), 'R8': (40.0, 54.6, 0, 'F'),
 'C10': (41.5, 49.5, 0, 'F'), 'C11': (41.5, 52.0, 0, 'F'), 'TP5': (44.5, 56.0, 0, 'F'), 'JP1': (35.0, 57.0, 0, 'F'),
 'FID1': (9.5, 4.0, 0, 'F'), 'FID2': (71.0, 33.0, 0, 'F'),
 'H1': (4.0, 4.0, 0, 'F'), 'H2': (71.0, 4.0, 0, 'F'), 'H3': (4.0, 62.0, 0, 'F'), 'H4': (71.0, 62.0, 0, 'F'),
}

def build(path):
    b = pcbnew.BOARD()
    b.SetCopperLayerCount(2)
    ds = b.GetDesignSettings()
    ds.m_TrackMinWidth = mm(0.2); ds.m_MinClearance = mm(0.2); ds.m_ViasMinSize = mm(0.6); ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3); ds.m_HoleToHoleMin = mm(0.25)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(0.25)); nc.SetClearance(mm(0.2)); nc.SetViaDiameter(mm(0.6)); nc.SetViaDrill(mm(0.3))
    # outline
    pts = [(0, 0), (W, 0), (W, H), (0, H)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        a, c = pts[i], pts[(i + 1) % 4]
        s.SetStart(pcbnew.VECTOR2I(mm(OX + a[0]), mm(OY + a[1]))); s.SetEnd(pcbnew.VECTOR2I(mm(OX + c[0]), mm(OY + c[1])))
        b.Add(s)
    nets = {}
    def net(n):
        if n not in nets:
            ni = pcbnew.NETINFO_ITEM(b, n); b.Add(ni); nets[n] = ni
        return nets[n]
    for ref, (lib, fpn, val, mpn, mfr, desc, padmap) in PARTS.items():
        fp = pcbnew.FootprintLoad(L + lib + '.pretty', fpn)
        assert fp, (ref, fpn)
        fp.SetReference(ref); fp.SetValue(val)
        x, y, rot, side = POS[ref]
        fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))
        fp.SetOrientationDegrees(rot)
        b.Add(fp)
        if ref == 'U1':
            # 0.2 mm thermal vias are below the standard drill size: replace them by four 0.3 mm vias
            for p in list(fp.Pads()):
                if p.GetNumber() == '19' and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                    fp.Remove(p)
            for dx, dy in ((0.41, -0.35), (1.51, -0.35), (0.41, 0.75), (1.51, 0.75)):
                v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(OX + x + dx), mm(OY + y + dy)))
                v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(net('GND')); b.Add(v)
        for p in fp.Pads():
            n = padmap.get(p.GetNumber())
            if n: p.SetNet(net(n))
        if ref.startswith('H') or ref.startswith('FID'): fp.Reference().SetVisible(False)
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); fp.Reference().SetTextThickness(mm(0.12))
        fp.Value().SetVisible(False)
    # power net class
    pw = pcbnew.NETCLASS('Power'); pw.SetTrackWidth(mm(0.6)); pw.SetClearance(mm(0.2)); pw.SetViaDiameter(mm(0.8)); pw.SetViaDrill(mm(0.4))
    ds.m_NetSettings.m_NetClasses['Power'] = pw
    add_silk(b)
    for n, w, pts in PREROUTE:
        for a, e in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(b); t.SetLayer(pcbnew.F_Cu); t.SetWidth(mm(w)); t.SetNet(net(n))
            t.SetStart(pcbnew.VECTOR2I(mm(OX + a[0]), mm(OY + a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(OX + e[0]), mm(OY + e[1])))
            b.Add(t)
    b.BuildConnectivity()
    pcbnew.SaveBoard(path, b)
    set_classes(path.replace('.kicad_pcb', '.kicad_pro'))
    return b


# Tracks laid before autorouting: net, width, points (board mm)
PREROUTE = [
    ('+3V3', 0.5, [(10.75, 12.6), (10.75, 23.1)]),   # C8 -> pull-ups R3/R2 on the left of the module
]

SILK = [  # text, x, y, size, rotation
    ('camper-heater LIN v1.0', 52.0, 3.0, 1.2, 0),
    ('+12V', 1.2, 33.0, 1.0, 0), ('GND', 1.2, 44.6, 1.0, 0), ('1 A', 13.5, 30.0, 0.9, 0),
    ('RESET', 3.0, 11.0, 0.8, 0), ('BOOT', 3.0, 27.5, 0.8, 0),
    ('PWR', 44.0, 8.0, 0.8, 0), ('LIN', 48.0, 8.0, 0.8, 0), ('MQTT', 52.0, 8.0, 0.8, 0),
    ('CP Plus', 15.0, 45.6, 0.9, 0), ('Heizung', 55.0, 45.6, 0.9, 0),
    ('JP1 = RJ12 Pin 5 an GND', 35.0, 60.5, 0.8, 0), ('erst nach Messen schliessen', 35.0, 62.0, 0.8, 0),
    ('USB-C', 66.5, 26.0, 0.8, 0),
]

def add_silk(b):
    for t, x, y, s, r in SILK:
        tx = pcbnew.PCB_TEXT(b); tx.SetText(t); tx.SetLayer(pcbnew.F_SilkS)
        tx.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); tx.SetTextSize(pcbnew.VECTOR2I(mm(s), mm(s)))
        tx.SetTextThickness(mm(max(0.12, s * 0.15))); tx.SetTextAngleDegrees(r)
        if x < 3: tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
        b.Add(tx)

def set_classes(pro_path):
    import json
    d = json.load(open(pro_path))
    ns = d['net_settings']
    for c in ns['classes']:
        if c['name'] == 'Power': c['track_width'] = 0.5; c['clearance'] = 0.2
    ns['classes'].append(dict(ns['classes'][1], name='Supply', track_width=0.8, clearance=0.3))
    pats = [{'netclass': 'Supply', 'pattern': n} for n in ('VIN_RAW', 'VIN_F', 'VBAT')]
    pats += [{'netclass': 'Power', 'pattern': n} for n in ('+5V_DC', '+5V', 'VBUS', '+3V3', 'GND')]
    ns['netclass_patterns'] = pats
    json.dump(d, open(pro_path, 'w'), indent=2)

if __name__ == '__main__':
    build(sys.argv[1])
