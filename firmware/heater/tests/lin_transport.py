import sys, re, glob, os
ESPHOME = re.compile(r'\[(\d+):(\d+):([\d.]+)\]\[V\]\[truma_inetbox\.LinBusListener:\d+\]: PID ([0-9A-F]{2})\s+((?:[0-9A-F]{2}\.)*[0-9A-F]{2}) \((\d+)\)')
def frames(path):
    for line in open(path, errors='replace'):
        e = ESPHOME.search(line)
        if e:
            # ESPHome truma_inetbox verbose log: data bytes followed by the LIN checksum
            h, mi, sec, pid, data, n = e.groups()
            d = [int(x, 16) for x in data.split('.')]
            if len(d) == 9:
                yield int(h)*3600 + int(mi)*60 + float(sec), int(pid, 16), d[:8]
            continue
        m = re.match(r'\s*([\d,\.]+)\s+([0-9A-F]{2})\s+((?:[0-9A-F]{2} ){1,8}[0-9A-F]{2}|[0-9A-F]{2})?\s+(\d{4})\s*(.*)$', line.strip())
        if not m: continue
        t, pid, data, baud, err = m.groups()
        if err.strip() or not data: continue
        yield float(t.replace(',','.')), int(pid,16), [int(x,16) for x in data.split()]
def transport(path):
    """Reassemble diagnostic transport messages. Yields (t, dir, nad, payload)."""
    buf = {}
    for t, pid, d in frames(path):
        p = pid & 0x3F
        if p not in (0x3C, 0x3D) or len(d) < 8: continue
        dr = 'M' if p == 0x3C else 'S'
        nad, pci = d[0], d[1]
        if d == [0xFF]*8: continue
        if pci & 0xF0 == 0x00:
            yield t, dr, nad, d[2:2+pci]
        elif pci & 0xF0 == 0x10:
            ln = ((pci & 0x0F) << 8) | d[2]
            buf[dr] = [ln, d[3:8], 1, nad]
        elif pci & 0xF0 == 0x20 and dr in buf:
            ln, b, cnt, n = buf[dr]
            if (pci & 0x0F) != (cnt & 0x0F): buf.pop(dr); continue
            b = b + d[2:8]; buf[dr] = [ln, b, cnt+1, n]
            if len(b) >= ln:
                buf.pop(dr); yield t, dr, n, b[:ln]
if __name__ == '__main__':
    for t, dr, nad, pl in transport(sys.argv[1]):
        print(f"{t:10.3f} {dr} NAD={nad:02X} len={len(pl):2d} " + ' '.join(f'{x:02X}' for x in pl))
