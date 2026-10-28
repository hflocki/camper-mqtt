"""Add GND pours on both layers plus stitching vias, fill and save.

usage: python3 finish.py routed.kicad_pcb final.kicad_pcb
"""
import math
import re
import shutil
import sys

import pcbnew

OX, OY, W, H = 100, 100, 75, 66
mm = pcbnew.FromMM
V = lambda x, y: pcbnew.VECTOR2I(mm(x), mm(y))


def add_pours(b, gnd):
    for z in list(b.Zones()):
        if not z.GetIsRuleArea():
            b.Remove(z)
    for lay in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(b)
        z.SetLayer(lay)
        z.SetNet(gnd)
        o = z.Outline()
        o.NewOutline()
        for x, y in ((0.2, 0.2), (W - 0.2, 0.2), (W - 0.2, H - 0.2), (0.2, H - 0.2)):
            o.Append(mm(OX + x), mm(OY + y))
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.4))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetZoneName('GND_' + ('F' if lay == pcbnew.F_Cu else 'B'))
        b.Add(z)


def keepouts(b):
    out = []
    for fp in b.GetFootprints():
        for z in fp.Zones():
            if z.GetIsRuleArea():
                out.append(z)
    return out


def free(b, x, y, ko):
    """True if a 0.6 mm GND via fits at (x, y) mm (absolute) inside both pours."""
    p = V(x, y)
    for z in ko:
        if z.Outline().Collide(p, mm(1.0)):
            return False
    for z in b.Zones():
        if z.GetIsRuleArea():
            continue
        fill = z.GetFilledPolysList(z.GetLayer())
        for dx, dy in ((0, 0), (0.55, 0), (-0.55, 0), (0, 0.55), (0, -0.55), (0.4, 0.4), (-0.4, 0.4), (0.4, -0.4), (-0.4, -0.4)):
            if not fill.Contains(V(x + dx, y + dy)):
                return False
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.HitTest(p, mm(0.9)):
                return False
    for t in b.GetTracks():
        if t.HitTest(p, mm(0.8)):
            return False
    return True


def free_tight(b, x, y, ko, fill_f, idx, fill_b):
    """Relaxed check for small pour islands: keep 0.55 mm from other nets, 0.25 mm from GND pads."""
    p = V(x, y)
    for z in ko:
        if z.Outline().Collide(p, mm(0.8)):
            return False
    for dx, dy in ((0, 0), (0.35, 0), (-0.35, 0), (0, 0.35), (0, -0.35)):
        q = V(x + dx, y + dy)
        if not fill_f.Contains(q, idx) or not fill_b.Contains(q):
            return False
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            same = pad.GetNetname() == 'GND'
            if pad.HitTest(p, mm(0.55 if same else 0.6)) and (not same or pad.HitTest(p, mm(0.55))):
                if not same or pad.HitTest(p, mm(0.55)):
                    return False
    for t in b.GetTracks():
        if t.GetNetname() != 'GND' and t.HitTest(p, mm(0.6)):
            return False
        if isinstance(t, pcbnew.PCB_VIA) and t.HitTest(p, mm(0.65)):
            return False
    return True


def via(b, gnd, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y))
    v.SetWidth(mm(0.6))
    v.SetDrill(mm(0.3))
    v.SetNet(gnd)
    b.Add(v)


def fill(b):
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())


def unconnected_gnd_pads(b, tmp):
    pcbnew.WriteDRCReport(b, tmp, pcbnew.EDA_UNITS_MILLIMETRES, True)
    txt = open(tmp).read()
    pads = set()
    for blk in re.split(r'\n(?=\[)', txt):
        if blk.startswith('[unconnected_items]'):
            for m in re.finditer(r'@\(([\d.]+) mm, ([\d.]+) mm\): Pad (\S+) \[GND\] of (\S+)', blk):
                pads.add((m.group(4), m.group(3), float(m.group(1)), float(m.group(2))))
    return pads


def main():
    src, dst = sys.argv[1], sys.argv[2]
    shutil.copy(src.replace('.kicad_pcb', '.kicad_pro'), dst.replace('.kicad_pcb', '.kicad_pro'))
    b = pcbnew.LoadBoard(src)
    gnd = b.FindNet('GND')
    ko = keepouts(b)
    add_pours(b, gnd)
    fill(b)
    added = 0
    for yy in [3.0 + 3.0 * i for i in range(21)]:
        for xx in [3.0 + 3.0 * i for i in range(24)]:
            if yy < H - 1.5 and xx < W - 1.5 and free(b, OX + xx, OY + yy, ko):
                via(b, gnd, OX + xx, OY + yy)
                added += 1
    fill(b)
    print('stitching vias', added)
    # every island of a pour needs at least one via (or GND through-hole pad) to the other layer
    isl = 0
    for _ in range(3):
        anchors = [t.GetPosition() for t in b.GetTracks() if isinstance(t, pcbnew.PCB_VIA) and t.GetNetname() == 'GND']
        anchors += [pd.GetPosition() for fp in b.GetFootprints() for pd in fp.Pads()
                    if pd.GetNetname() == 'GND' and pd.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
        new = 0
        for z in b.Zones():
            if z.GetIsRuleArea():
                continue
            f = z.GetFilledPolysList(z.GetLayer())
            for i in range(f.OutlineCount()):
                if any(f.Contains(a, i) for a in anchors):
                    continue
                bb = f.Outline(i).BBox()
                x0, y0 = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
                x1, y1 = pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())
                spot = None
                yy = y0 + 0.6
                while yy < y1 and not spot:
                    xx = x0 + 0.6
                    while xx < x1:
                        if f.Contains(V(xx, yy), i) and free(b, xx, yy, ko):
                            spot = (xx, yy)
                            break
                        xx += 0.25
                    yy += 0.25
                if not spot:
                    fb = [zz for zz in b.Zones() if not zz.GetIsRuleArea() and zz.GetLayer() != z.GetLayer()][0]
                    fbf = fb.GetFilledPolysList(fb.GetLayer())
                    yy = y0 + 0.3
                    while yy < y1 and not spot:
                        xx = x0 + 0.3
                        while xx < x1:
                            if free_tight(b, xx, yy, ko, f, i, fbf):
                                spot = (xx, yy)
                                break
                            xx += 0.1
                        yy += 0.1
                if spot:
                    via(b, gnd, *spot)
                    new += 1
                else:
                    print('island without via:', [round(v - 100, 1) for v in (x0, y0, x1, y1)])
        isl += new
        fill(b)
        if not new:
            break
    print('island vias', isl)
    tmp = dst.replace('.kicad_pcb', '-tmp-drc.txt')
    extra = 0
    for _ in range(4):
        todo = unconnected_gnd_pads(b, tmp)
        if not todo:
            break
        for ref, num, x, y in sorted(todo):
            spot = None
            for r in (1.0, 1.3, 1.7, 2.2, 2.8):
                for k in range(24):
                    a = 2 * math.pi * k / 24
                    if free(b, x + r * math.cos(a), y + r * math.sin(a), ko):
                        spot = (x + r * math.cos(a), y + r * math.sin(a))
                        break
                if spot:
                    break
            if spot:
                via(b, gnd, *spot)
                extra += 1
            else:
                print('no room for a via next to', ref, num)
        fill(b)
    print('vias next to cut-off GND pads', extra)
    # drill / place origin at the lower left board corner (used for Gerber, drill and placement files)
    b.GetDesignSettings().SetAuxOrigin(V(OX, OY + H))
    pcbnew.SaveBoard(dst, b)


if __name__ == '__main__':
    main()
