import sys, re
from lin_transport import ESPHOME
# Converts a LIN analyzer log (or an ESPHome truma_inetbox verbose log) into replay events.
# Slave-response slots (0x3D, 0x18) become "S pid <real answer or ->", everything else "M pid data".
def parse(path):
    for line in open(path, errors='replace'):
        e = ESPHOME.search(line)
        if e:
            d = e.group(5).split('.')
            if len(d) == 9: yield int(e.group(4), 16), d[:8]
            continue
        m = re.match(r'\s*([\d,\.]+)\s+([0-9A-F]{2})\s+((?:[0-9A-F]{2} ){1,8}[0-9A-F]{2}|[0-9A-F]{2})?\s+(\d{4})\s*(.*)$', line.strip())
        if not m: continue
        t, pid, data, baud, err = m.groups()
        yield int(pid,16) & 0x3F, (data.split() if data and not err.strip() else None)
out = []
for pid, d in parse(sys.argv[1]):
    if pid in (0x3D, 0x18):
        out.append(f"S {pid:02X} " + (' '.join(d) if d else '-'))
    elif d and len(d) == 8:
        out.append(f"M {pid:02X} " + ' '.join(d))
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
print(len(out), 'events')
