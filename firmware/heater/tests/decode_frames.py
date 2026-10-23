import sys, glob, os, struct, collections
sys.path.insert(0,'.')
from lin_transport import transport
def cs(m, s):
    for b in m:
        s += b
        if s >= 256: s -= 255
    return (~s) & 0xFF
TYPES={0x0A:'INIT_REQ',0x0B:'DEVICES',0x0D:'ACK',0x14:'CLOCK_RESP',0x15:'CLOCK',0x17:'CONFIG',0x32:'HEATER_RESP',0x33:'HEATER',0x3D:'TIMER',0x3C:'TIMER_RESP',0x35:'AC_MAN',0x37:'AC_AUTO'}
def t2c(v): return None if v==0 else round(v/10-273,1)
def dec(pl):
    if len(pl) < 17 or pl[11]!=0x54: return None
    ln, typ, cnt, ck = pl[13], pl[14], pl[15], pl[16]
    body = pl[17:17+ln]
    pad = (pl+[0]*41)[:41]
    ok = ck == cs(pad[10:41], 0xFF-ck)
    s = f"{TYPES.get(typ,hex(typ)):11s} cnt={cnt:02X} ck={'ok' if ok else 'BAD'} "
    if typ in (0x33,0x32):
        tr,hm,ea,tw,eb = struct.unpack_from('<HHHHH', bytes(body))
        s += f"room={t2c(tr)} mode=0x{hm:X} elA={ea} water={t2c(tw)} elB={eb} mixA={body[10]} mixB={body[11]}"
        if typ==0x33:
            cw,cr = struct.unpack_from('<HH', bytes(body),12)
            s += f" cur_water={t2c(cw)} cur_room={t2c(cr)} status={body[16]} err={body[18]}{body[17]:02d} unk={body[19]}"
    elif typ==0x0B:
        s += f"dev {body[1]+1}/{body[0]} sw={body[7]}.{body[8]:02d}.{body[9]:02d} hw={body[5]<<8|body[4]:04X}.{body[6]:02X}"
    elif typ==0x0D:
        s += f"result={body[0]}"
    elif typ in (0x15,0x14):
        s += f"{body[0]:02d}:{body[1]:02d}:{body[2]:02d} disp={body[3:6]} mode={body[6]} src={body[7]}"
    else:
        s += ' '.join(f'{x:02X}' for x in body)
    return s
if __name__=='__main__':
    for f in sys.argv[1:]:
        print('=====', os.path.basename(f))
        for t, dr, nad, pl in transport(f):
            if pl and pl[0] in (0xBA,0xBB,0xFA,0xFB) and len(pl)>11:
                d = dec(pl)
                if d: print(f"{t:9.2f} {'CP→' if dr=='M' else '←iB'} {pl[0]:02X} len={len(pl):2d} {d}")
