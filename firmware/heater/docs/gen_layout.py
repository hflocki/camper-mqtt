#!/usr/bin/env python3
"""Generate the perfboard layout (Bestückungs-/Verdrahtungsplan) as SVG + PNG."""
import sys
import cairosvg

out = sys.argv[1]
P = 24                 # pixel per 2.54 mm
X0, Y0 = 280, 150      # origin: hole (0,0)
COLS, ROWS = 35, 28
W, H = 1920, 1100
s = []
a = s.append

C12, C5, C33, CG, CL, CTX, CRX = '#d62728', '#ff7f0e', '#c2185b', '#111', '#1f5fbf', '#2ca02c', '#17becf'


def hx(x):
    return X0 + x * P


def hy(y):
    return Y0 + y * P


def txt(x, y, t, size=13, anchor='start', col='#111', bold=False):
    b = ' font-weight="bold"' if bold else ''
    a(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" fill="{col}"{b}>{t}</text>')


def tbg(x, y, t, size=12, anchor='start', col='#111', bold=True):
    w = len(t) * size * 0.62
    x0 = x if anchor == 'start' else (x - w / 2 if anchor == 'middle' else x - w)
    a(f'<rect x="{x0-3}" y="{y-size}" width="{w+6}" height="{size+5}" rx="3" fill="#fff" fill-opacity="0.88"/>')
    txt(x, y, t, size, anchor, col, bold)


def wire(pts, col, w=4, dash=None):
    """Insulated wire along hole coordinates (grid units)."""
    p = ' '.join(f'{hx(x)},{hy(y)}' for x, y in pts)
    d = f' stroke-dasharray="{dash}"' if dash else ''
    a(f'<polyline points="{p}" fill="none" stroke="{col}" stroke-width="{w}" stroke-linejoin="round" '
      f'stroke-linecap="round" opacity="0.9"{d}/>')


def flying(p0, p1, col, bend=(0, 0)):
    """Flying wire to an off-board part, drawn as a curve. Points in pixels."""
    (x0, y0), (x1, y1) = p0, p1
    cx, cy = (x0 + x1) / 2 + bend[0], (y0 + y1) / 2 + bend[1]
    a(f'<path d="M{x0},{y0} Q{cx},{cy} {x1},{y1}" fill="none" stroke="{col}" stroke-width="4" '
      f'stroke-linecap="round" opacity="0.9"/>')


def solder(x, y, col='#111'):
    a(f'<circle cx="{hx(x)}" cy="{hy(y)}" r="6" fill="{col}" stroke="#fff" stroke-width="1.5"/>')


def label_hole(x, y, t, dx=10, dy=-8, col='#111', size=12, anchor='start'):
    tbg(hx(x) + dx, hy(y) + dy, t, size, anchor, col, True)


a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
  'font-family="DejaVu Sans, Arial, sans-serif">')
a(f'<rect width="{W}" height="{H}" fill="#fff"/>')
txt(40, 46, 'Aufbau auf Lochraster (Punktraster 2,54 mm, ca. 9 × 7,5 cm) – Draufsicht Bestückungsseite', 24, bold=True)
txt(40, 74, 'Koordinate (Spalte|Reihe). Farbige Linien = isolierte Drahtbrücken, Punkt = Lötstelle. '
    'Kreuzungen ohne Punkt sind nicht verbunden.', 14, col='#555')

# ---------------- board ----------------
a(f'<rect x="{hx(0.3)}" y="{hy(0.3)}" width="{(COLS+0.4)*P}" height="{(ROWS+0.4)*P}" rx="10" '
  'fill="#f3e3b8" stroke="#b08a3c" stroke-width="2"/>')
for x in range(1, COLS + 1):
    for y in range(1, ROWS + 1):
        a(f'<circle cx="{hx(x)}" cy="{hy(y)}" r="5" fill="#d9a441" stroke="#a9772a" stroke-width="1"/>'
          f'<circle cx="{hx(x)}" cy="{hy(y)}" r="2" fill="#fff"/>')
for x in range(1, COLS + 1):
    if x == 1 or x % 5 == 0:
        txt(hx(x), hy(0.3) - 8, str(x), 11, 'middle', '#777')
for y in range(1, ROWS + 1):
    if y == 1 or y % 5 == 0:
        txt(hx(0.3) - 8, hy(y) + 4, str(y), 11, 'end', '#777')

# ---------------- ESP32-S3-DevKitC-1 headers ----------------
j1 = {1: '3V3', 2: '3V3', 3: 'RST', 4: '4', 5: '5', 6: '6', 7: '7', 8: '15', 9: '16', 10: '17', 11: '18',
      12: '8', 13: '3', 14: '46', 15: '9', 16: '10', 17: '11', 18: '12', 19: '13', 20: '14', 21: '5V', 22: 'GND'}
JX, JY0, J3X = 24, 6, 33
a(f'<rect x="{hx(JX-0.6)}" y="{hy(JY0-2.2)}" width="{(J3X-JX+1.2)*P}" height="{25.0*P}" rx="10" '
  'fill="#2b5fae" fill-opacity="0.10" stroke="#2b5fae" stroke-width="2" stroke-dasharray="8,5"/>')
tbg((hx(JX) + hx(J3X)) / 2, hy(JY0 - 1.4), 'ESP32-S3-DevKitC-1 N8R8 (aufgesteckt)', 13, 'middle', '#2b5fae', True)
txt((hx(JX) + hx(J3X)) / 2, hy(JY0 + 25.0) + 4, 'USB-Buchsen zeigen über den unteren Rand', 12, 'middle', '#2b5fae')
for i in range(22):
    for xx, col in ((JX, '#333'), (J3X, '#999')):
        a(f'<rect x="{hx(xx)-8}" y="{hy(JY0+i)-8}" width="16" height="16" fill="{col}" rx="2"/>')
    name = j1[i + 1]
    imp = name in ('3V3', '17', '18', '5V', 'GND')
    lab = ('GPIO' + name) if name.isdigit() else name
    if imp:
        tbg(hx(JX) + 14, hy(JY0 + i) + 4, lab, 11, 'start', '#000', True)
tbg(hx(J3X), hy(JY0 + 23.6), 'J3 (nur Halt)', 11, 'middle', '#555')
tbg(hx(JX), hy(JY0 + 23.6), 'J1', 11, 'middle', '#333', True)

# ---------------- GND bus ----------------
wire([(2, 19), (2, 27), (24, 27)], CG, 5)
# ---------------- JST 12 V pass-through ----------------
for (yy, name) in ((18, 'JST IN (Kabelbaum)'), (23, 'JST OUT (CP Plus)')):
    a(f'<rect x="{hx(1)-62}" y="{hy(yy)-14}" width="52" height="{P+28}" rx="4" fill="#ddd" stroke="#888"/>')
    txt(hx(1) - 70, hy(yy) - 20, name, 12, 'end', '#333', True)
    txt(hx(1) - 20, hy(yy) + 5, '+', 14, 'middle', C12, True)
    txt(hx(1) - 20, hy(yy + 1) + 5, '−', 14, 'middle', CG, True)
wire([(1, 18), (3, 18), (3, 23), (1, 23)], C12, 4)      # + through
wire([(1, 19), (2, 19)], CG, 4)                          # − IN to bus
wire([(1, 24), (2, 24)], CG, 4)                          # − OUT to bus
# ---------------- fuse tap, D1 bridge, 12 V bus ----------------
wire([(3, 18), (5, 18)], C12, 4)
wire([(7, 18), (10, 18)], C12, 4, '7,5')                 # D1 position (bridge or 1N400x)
wire([(10, 18), (17, 18)], C12, 5)
# fuse holder (off board, above)
fx, fy = hx(6), hy(15.4)
a(f'<rect x="{fx-34}" y="{fy-14}" width="68" height="28" rx="6" fill="#222"/>')
txt(fx, fy + 5, 'F1 1 A', 12, 'middle', '#fff', True)
flying((hx(5), hy(18)), (fx - 34, fy), C12, (-10, 0))
flying((hx(7), hy(18)), (fx + 34, fy), C12, (10, 0))
# D2 TVS (12,18) ring -> (12,22)
a(f'<rect x="{hx(12)-8}" y="{hy(18.6)}" width="16" height="{2.8*P}" rx="4" fill="#222"/>')
a(f'<rect x="{hx(12)-8}" y="{hy(18.6)}" width="16" height="6" fill="#ddd"/>')
wire([(12, 22), (12, 27)], CG, 4)
# C1 (+ 14,19 / - 14,20)
wire([(14, 18), (14, 19)], C12, 4)
a(f'<circle cx="{hx(14)}" cy="{(hy(19)+hy(20))/2}" r="17" fill="#2a2a6a" stroke="#000"/>')
txt(hx(14) - 2, hy(19) + 2, '+', 12, 'middle', '#fff', True)
wire([(14, 20), (14, 27)], CG, 4)

# ---------------- ESP side ----------------
wire([(21, 6), (24, 6)], C33, 4)                         # 3V3 from J1-1
wire([(20, 6), (21, 6)], C33, 4)
wire([(21, 6), (21, 10)], C33, 4)
a(f'<rect x="{hx(21)-7}" y="{hy(10.4)}" width="14" height="{3.2*P}" rx="4" fill="#c9b48a" stroke="#7a5f2a"/>')
wire([(21, 14), (21, 16), (24, 16)], CRX, 4)             # R1 bottom -> RX node -> GPIO18
wire([(23, 15), (24, 15)], CTX, 4)                       # TX node -> GPIO17
wire([(20, 26), (24, 26)], C5, 5)                        # 5V

# ---------------- LIN module (mounted on board with spacers / glue) ----------------
mx0, my0, mx1, my1 = 4.6, 2.2, 15.4, 10.6
a(f'<rect x="{hx(mx0)}" y="{hy(my0)}" width="{(mx1-mx0)*P}" height="{(my1-my0)*P}" rx="8" '
  'fill="#2e9e55" fill-opacity="0.85" stroke="#1d6b39" stroke-width="2"/>')
txt((hx(mx0) + hx(mx1)) / 2, hy(6.6), 'LIN-Modul T151', 15, 'middle', '#fff', True)
txt((hx(mx0) + hx(mx1)) / 2, hy(7.5), 'Abstandshalter / Heißkleber', 11, 'middle', '#eafff0')
top = {'GND': 6, 'RX': 8.7, 'SLP': 11.3, 'TX': 14}
bot = {'GND': 6, 'LIN': 8.7, 'INH': 11.3, '12V': 14}
for n, x in top.items():
    a(f'<circle cx="{hx(x)}" cy="{hy(my0+0.6)}" r="9" fill="#9be3a9" stroke="#1d6b39" stroke-width="2"/>')
    txt(hx(x), hy(my0 + 1.6), n, 12, 'middle', '#fff', True)
for n, x in bot.items():
    a(f'<circle cx="{hx(x)}" cy="{hy(my1-0.6)}" r="9" fill="#9be3a9" stroke="#1d6b39" stroke-width="2"/>')
    txt(hx(x), hy(my1 - 1.2), n, 12, 'middle', '#fff' if n != 'INH' else '#cfe', True)
tp = lambda n: (hx(top[n]), hy(my0 + 0.6))
bp = lambda n: (hx(bot[n]), hy(my1 - 0.6))
flying(tp('TX'), (hx(23), hy(15)), CTX, (90, -40))
flying(tp('RX'), (hx(21), hy(16)), CRX, (120, -170))
flying(tp('SLP'), (hx(20), hy(6)), C33, (40, -110))
flying(bp('12V'), (hx(17), hy(18)), C12, (40, 0))
flying(bp('GND'), (hx(7), hy(27)), CG, (-60, 40))

# ---------------- RJ12 IN / OUT (glued to the board edge, off grid) ----------------
for i, (yy, name) in enumerate(((2.0, 'RJ12 IN (Heizung)'), (9.6, 'RJ12 OUT (CP Plus)'))):
    rx, ry = 40, hy(yy)
    a(f'<rect x="{rx}" y="{ry}" width="96" height="120" rx="6" fill="#3a3a3a"/>')
    txt(rx, ry - 8, name, 12, 'start', '#333', True)
    for k in range(6):
        a(f'<circle cx="{rx+100}" cy="{ry+16+k*18}" r="4" fill="#bbb"/>')
        txt(rx + 88, ry + 20 + k * 18, str(k + 1), 11, 'end', '#fff')
# 1:1 pass-through between the sockets
for k in range(6):
    y1, y2 = hy(2.0) + 16 + k * 18, hy(9.6) + 16 + k * 18
    col = CL if k == 2 else (CG if k == 4 else '#888')
    flying((144, y1), (144, y2), col, (30 + k * 10, 0))
# LIN tap (pin 3 IN) -> module LIN
flying((144, hy(2.0) + 16 + 2 * 18), bp('LIN'), CL, (-40, 140))
# GND tap (pin 5 IN) -> GND bus
flying((144, hy(2.0) + 16 + 4 * 18), (hx(2), hy(21)), CG, (-60, 160))

# ---------------- DC/DC converter (off board, below) ----------------
cx, cy = hx(12), hy(31.2)
a(f'<rect x="{cx-110}" y="{cy-26}" width="220" height="52" rx="6" fill="#111"/>')
txt(cx, cy - 4, 'DC/DC Bauer 8–32 V → 5 V', 13, 'middle', '#fff', True)
txt(cx, cy + 14, '(vergossen, liegt neben der Platine)', 11, 'middle', '#ccc')
flying((cx - 80, cy - 26), (hx(16), hy(18)), C12, (-170, 0))
flying((cx - 40, cy - 26), (hx(16), hy(27)), CG, (0, 0))
flying((cx + 40, cy - 26), (hx(19), hy(27)), CG, (0, 0))
flying((cx + 80, cy - 26), (hx(20), hy(26)), C5, (30, 0))
txt(cx - 80, cy + 44, 'rot', 11, 'middle'); txt(cx - 40, cy + 44, 'schw.', 11, 'middle')
txt(cx + 40, cy + 44, 'schw.', 11, 'middle'); txt(cx + 80, cy + 44, 'gelb', 11, 'middle')

# ---------------- solder points ----------------
for x, y, c in [(1, 18, C12), (1, 19, CG), (1, 23, C12), (1, 24, CG), (3, 18, C12), (5, 18, C12), (7, 18, C12),
                (10, 18, C12), (12, 18, C12), (14, 18, C12), (16, 18, C12), (17, 18, C12),
                (12, 22, CG), (14, 19, C12), (14, 20, CG), (2, 19, CG), (2, 24, CG), (2, 27, CG), (2, 21, CG),
                (7, 27, CG), (12, 27, CG), (14, 27, CG), (16, 27, CG), (19, 27, CG), (24, 27, CG),
                (20, 26, C5), (24, 26, C5), (24, 6, C33), (21, 6, C33), (20, 6, C33), (21, 10, C33),
                (21, 14, CRX), (21, 16, CRX), (24, 16, CRX), (23, 15, CTX), (24, 15, CTX)]:
    solder(x, y, c)

# labels on board
label_hole(10, 18, '12 V abgesichert', -50, -12, C12, 12)
label_hole(5, 20.6, 'D1: Brücke oder 1N400x (Ring →)', 0, 0, '#555', 11)
label_hole(12, 22, 'D2 P6KE24A, Ring oben', 12, 10, '#333', 11)
label_hole(14, 20, 'C1 22 µF/35 V, + oben', 22, 4, '#333', 11)
label_hole(21, 12, 'R1 10 kΩ', -14, 4, '#333', 11, 'end')
label_hole(8, 28, 'Masse-Bus (Reihe 27)', 0, 5, CG, 12)

# ---------------- connection list (right) ----------------
LX, LY = 1240, 120
a(f'<rect x="{LX-20}" y="{LY-30}" width="660" height="975" rx="10" fill="#fafafa" stroke="#ddd"/>')
txt(LX, LY, 'Verdrahtungsliste', 18, bold=True)
rows = [
    ('Versorgung 12 V', None),
    ('1', 'JST IN + (1|18) → JST OUT + (1|23) über (3|18)–(3|23)'),
    ('2', 'JST IN − (1|19) und OUT − (1|24) → Masse (2|19)…(2|27)'),
    ('3', '(3|18) → (5|18) → Sicherungshalter F1 1 A → (7|18)'),
    ('4', '(7|18) → (10|18): Drahtbrücke (oder 1N400x, Ring rechts)'),
    ('5', '12-V-Leitung (10|18) … (17|18)'),
    ('6', 'D2 P6KE24A: Ring an (12|18), anderes Ende (12|22) → Masse'),
    ('7', 'C1 22 µF: + (14|19) an 12 V, − (14|20) → Masse'),
    ('8', 'Wandler rot → (16|18), schwarz (Eingang) → (16|27)'),
    ('9', 'Wandler gelb → (20|26) → (24|26) = 5V, schwarz → (19|27)'),
    ('Masse', None),
    ('10', 'Masse-Bus Reihe 27: (2|27) … (24|27) = DevKit GND (J1-22)'),
    ('LIN-Modul', None),
    ('11', '12V-Klemme → (17|18)    GND unten → (7|27)'),
    ('12', 'LIN-Klemme → RJ12 IN Kontakt 3 (vorher messen)'),
    ('13', 'TX → (23|15) → (24|15) = GPIO17 (J1-10)'),
    ('14', 'RX → (21|16) → (24|16) = GPIO18 (J1-11)'),
    ('15', 'SLP → (20|6) → (21|6) = 3V3'),
    ('16', 'INH frei, GND oben frei (intern = GND unten, messen)'),
    ('ESP / Pull-up', None),
    ('17', '3V3 (24|6) → (21|6);  R1 10 kΩ von (21|10) nach (21|14)'),
    ('18', '(21|6)–(21|10) und (21|14)–(21|16) als Brücke'),
    ('RJ12', None),
    ('19', 'RJ12 IN ↔ RJ12 OUT: alle 6 Kontakte 1:1 durch'),
    ('20', 'RJ12 Kontakt 5 (GND, messen) → Masse (2|21)'),
    ('Reihenfolge', None),
    ('a', 'Buchsenleisten aufs DevKit stecken, so auf die Platine setzen, löten'),
    ('b', 'Masse-Bus und 12-V-Leitung, dann D2, C1, R1, Brücken'),
    ('c', 'JST-Buchsen, Sicherungshalter, Wandler-Kabel anlöten'),
    ('d', 'LIN-Modul befestigen, Klemmen verdrahten, RJ12 zuletzt'),
    ('Vor dem ersten Anschluss', None),
    ('✓', 'Durchgang: RJ12 IN ↔ OUT je Kontakt, keine Nachbarn'),
    ('✓', 'Kein Durchgang 12 V ↔ Masse, 5 V ↔ Masse, 3V3 ↔ Masse'),
    ('✓', 'Modul: Widerstand 12V↔LIN ≥ 30 kΩ (kein Master-R)'),
    ('✓', '12 V anlegen ohne DevKit: 5 V an (24|26), RX am Modul ≤ 3,3 V'),
    ('✓', 'Polarität am CP-Plus-Stecker messen, erst dann einstecken'),
]
y = LY + 34
for k, t in rows:
    if t is None:
        y += 8
        txt(LX, y, k, 14, bold=True, col='#2b5fae')
        y += 22
        continue
    txt(LX + 4, y, k, 13, bold=True)
    txt(LX + 36, y, t, 13)
    y += 24

a('</svg>')
svg = '\n'.join(s)
open(out + '.svg', 'w').write(svg)
cairosvg.svg2png(bytestring=svg.encode(), write_to=out + '.png')
