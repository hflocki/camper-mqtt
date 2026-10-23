#!/usr/bin/env python3
"""Perfboard layout for the ESP32-C3 Super Mini build (arrangement as built: LIN module centre, C3 top right)."""
import sys
import cairosvg

out = sys.argv[1]
P = 24                 # pixel per 2.54 mm
X0, Y0 = 280, 150
COLS, ROWS = 35, 20
W, H = 1880, 900
LIST_X, LIST_H = 1200, 760
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
txt(40, 46, 'Aufbau auf Lochraster mit ESP32-C3 Super Mini (35 × 20 Löcher, ca. 9 × 5 cm)', 24, bold=True)
txt(40, 74, 'Draufsicht Bestückungsseite. Koordinate (Spalte|Reihe). Linien = isolierte Drahtbrücken, gestrichelt = auf der '
    'Lötseite, Punkt = Lötstelle. Kreuzungen ohne Punkt sind nicht verbunden.', 14, col='#555')

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


def under(p0, p1, col, bend=(0, 0)):
    """Wire on the solder side (dashed)."""
    (x0, y0), (x1, y1) = (hx(p0[0]), hy(p0[1])), (hx(p1[0]), hy(p1[1]))
    a(f'<path d="M{x0},{y0} Q{(x0+x1)/2+bend[0]},{(y0+y1)/2+bend[1]} {x1},{y1}" fill="none" stroke="{col}" '
      'stroke-width="4" stroke-dasharray="8,5" stroke-linecap="round"/>')


# ================= left: 12 V side =================
for (yy, name) in ((8, 'JST IN (Kabelbaum)'), (12, 'JST OUT (CP Plus)')):
    a(f'<rect x="{hx(1)-62}" y="{hy(yy)-14}" width="52" height="{P+28}" rx="4" fill="#ddd" stroke="#888"/>')
    tbg(hx(1) - 36, hy(yy + 1) + 34, name, 12, 'middle', '#333', True)
    txt(hx(1) - 20, hy(yy) + 5, '+', 14, 'middle', C12, True)
    txt(hx(1) - 20, hy(yy + 1) + 5, '−', 14, 'middle', CG, True)
wire([(1, 8), (3, 8), (3, 12), (1, 12)], C12, 4)         # + pass-through
wire([(1, 9), (2, 9)], CG, 4)
wire([(1, 13), (2, 13)], CG, 4)
wire([(2, 9), (2, 18), (13, 18)], CG, 5)                 # left GND bus
wire([(3, 8), (5, 8)], C12, 4)
wire([(7, 8), (9, 8)], C12, 4, '7,5')                    # D1 position
wire([(9, 8), (13, 8)], C12, 5)                          # 12 V protected
fx, fy = hx(6), hy(5.6)
flying((hx(5), hy(8)), (fx - 34, fy), C12, (-10, 0))
flying((hx(7), hy(8)), (fx + 34, fy), C12, (10, 0))
a(f'<rect x="{fx-34}" y="{fy-14}" width="68" height="28" rx="6" fill="#222"/>')
txt(fx, fy + 5, 'F1 1 A', 12, 'middle', '#fff', True)
# D2 ring (10|8) -> (10|12) -> GND
a(f'<rect x="{hx(10)-8}" y="{hy(8.6)}" width="16" height="{2.8*P}" rx="4" fill="#222"/>')
a(f'<rect x="{hx(10)-8}" y="{hy(8.6)}" width="16" height="6" fill="#ddd"/>')
wire([(10, 12), (10, 18)], CG, 4)
# C1 + (12|9), - (12|10)
wire([(12, 8), (12, 9)], C12, 4)
a(f'<circle cx="{hx(12)}" cy="{(hy(9)+hy(10))/2}" r="17" fill="#2a2a6a" stroke="#000"/>')
txt(hx(12) - 2, hy(9) + 2, '+', 12, 'middle', '#fff', True)
wire([(12, 10), (12, 18)], CG, 4)

# ================= centre: LIN module =================
mx0, my0, mx1, my1 = 13.6, 0.6, 23.4, 19.6
a(f'<rect x="{hx(mx0)}" y="{hy(my0)}" width="{(mx1-mx0)*P}" height="{(my1-my0)*P}" rx="8" '
  'fill="#2e9e55" fill-opacity="0.88" stroke="#1d6b39" stroke-width="2"/>')
txt((hx(mx0) + hx(mx1)) / 2, hy(9.6), 'LIN-Modul T151', 15, 'middle', '#fff', True)
txt((hx(mx0) + hx(mx1)) / 2, hy(10.5), 'aufgeklebt / Abstandshalter', 11, 'middle', '#eafff0')
top = {'GND': 15, 'RX': 17.3, 'SLP': 19.7, 'TX': 22}
bot = {'GND': 15, 'LIN': 17.3, 'INH': 19.7, '12V': 22}
for n, x in top.items():
    a(f'<circle cx="{hx(x)}" cy="{hy(1.4)}" r="10" fill="#9be3a9" stroke="#1d6b39" stroke-width="2"/>')
    txt(hx(x), hy(2.5), n, 12, 'middle', '#fff', True)
for n, x in bot.items():
    a(f'<circle cx="{hx(x)}" cy="{hy(18.8)}" r="10" fill="#9be3a9" stroke="#1d6b39" stroke-width="2"/>')
    txt(hx(x), hy(17.9), n, 12, 'middle', '#fff', True)
tp = lambda n: (hx(top[n]), hy(1.4))
bp = lambda n: (hx(bot[n]), hy(18.8))

# ================= right: ESP32-C3 Super Mini (USB up, power pins right) =================
LX_, RX_, Y1 = 27, 34, 1
left = ['5', '6', '7', '8', '9', '10', '20', '21']
right = ['5V', 'G', '3.3', '4', '3', '2', '1', '0']
USED = ('5V', 'G', '3.3', '4', '3')
a(f'<rect x="{hx(LX_-0.7)}" y="{hy(Y1-0.7)}" width="{(RX_-LX_+1.4)*P}" height="{8.4*P}" rx="8" '
  'fill="#111" fill-opacity="0.85" stroke="#2b5fae" stroke-width="2"/>')
a(f'<rect x="{(hx(LX_)+hx(RX_))/2-30}" y="{hy(Y1-1.3)}" width="60" height="34" rx="5" fill="#bbb" stroke="#777"/>')
txt((hx(LX_) + hx(RX_)) / 2, hy(Y1 - 0.5), 'USB-C', 11, 'middle', '#333', True)
txt((hx(LX_) + hx(RX_)) / 2, hy(Y1 + 3.6), 'ESP32-C3', 13, 'middle', '#fff', True)
txt((hx(LX_) + hx(RX_)) / 2, hy(Y1 + 4.4), 'Super Mini', 13, 'middle', '#fff', True)
for i in range(8):
    a(f'<rect x="{hx(LX_)-8}" y="{hy(Y1+i)-8}" width="16" height="16" fill="#999" rx="2"/>')
    a(f'<rect x="{hx(RX_)-8}" y="{hy(Y1+i)-8}" width="16" height="16" '
      f'fill="{"#e6c200" if right[i] in USED else "#999"}" rx="2"/>')
    txt(hx(LX_) + 13, hy(Y1 + i) + 4, left[i], 10, 'start', '#ccc')
    txt(hx(RX_) - 13, hy(Y1 + i) + 4, right[i], 11, 'end', '#fff' if right[i] in USED else '#aaa', right[i] in USED)

# solder side: right header -> transfer points row 10
under((34, 1), (35, 10), C5, (16, 0))
under((34, 2), (33, 10), CG, (10, 0))
under((34, 3), (32, 10), C33, (4, 0))
under((34, 4), (31, 10), CTX, (0, 0))
under((34, 5), (30, 10), CRX, (-6, 0))
# component side
wire([(35, 10), (35, 12)], C5, 5)                        # 5V landing (35|12)
wire([(33, 10), (33, 16)], CG, 5)                        # GND landing (33|14), (33|16)
wire([(30, 10), (25, 10)], CRX, 4)                       # RX (GPIO3)
wire([(31, 10), (31, 11), (27, 11)], CTX, 4)             # TX (GPIO4)
wire([(32, 10), (32, 12), (26, 12)], C33, 4)             # 3V3
a(f'<rect x="{hx(25)-7}" y="{hy(10.4)}" width="14" height="{3.2*P}" rx="4" fill="#c9b48a" stroke="#7a5f2a"/>')
wire([(25, 14), (26, 14), (26, 12)], C33, 4)             # R1 bottom -> 3V3

# module logic side -> ESP area (flying wires from the screw terminals)
flying(tp('RX'), (hx(28), hy(10)), CRX, (10, -10))
flying(tp('TX'), (hx(27), hy(11)), CTX, (-10, 10))
flying(tp('SLP'), (hx(28), hy(12)), C33, (20, 10))
flying(tp('GND'), (hx(33), hy(14)), CG, (40, 120))
# module bus side
flying(bp('12V'), (hx(13), hy(8)), C12, (-170, 40))
flying(bp('GND'), (hx(13), hy(18)), CG, (-20, 30))

# ---------------- RJ12 (glued to the left edge) ----------------
for (yy, name) in ((0.6, 'RJ12 IN (Heizung)'), (14.0, 'RJ12 OUT (CP Plus)')):
    rx, ry = 40, hy(yy)
    a(f'<rect x="{rx}" y="{ry}" width="96" height="120" rx="6" fill="#3a3a3a"/>')
    txt(rx, ry - 8, name, 12, 'start', '#333', True)
    for k in range(6):
        a(f'<circle cx="{rx+100}" cy="{ry+16+k*18}" r="4" fill="#bbb"/>')
        txt(rx + 88, ry + 20 + k * 18, str(k + 1), 11, 'end', '#fff')
for k in range(6):
    y1, y2 = hy(0.6) + 16 + k * 18, hy(14.0) + 16 + k * 18
    col = CL if k == 2 else (CG if k == 4 else '#888')
    flying((144, y1), (144, y2), col, (40 + k * 10, 0))
flying((144, hy(0.6) + 16 + 2 * 18), bp('LIN'), CL, (60, 260))
flying((144, hy(0.6) + 16 + 4 * 18), (hx(2), hy(16)), CG, (-40, 160))

# ---------------- DC/DC converter (below the board) ----------------
cx, cy = hx(18), hy(23.2)
a(f'<rect x="{cx-110}" y="{cy-26}" width="220" height="52" rx="6" fill="#111"/>')
txt(cx, cy - 4, 'DC/DC Bauer 8–32 V → 5 V', 13, 'middle', '#fff', True)
txt(cx, cy + 14, '(vergossen, neben der Platine)', 11, 'middle', '#ccc')
flying((cx - 80, cy - 26), (hx(11), hy(8)), C12, (-260, 0))
flying((cx - 40, cy - 26), (hx(11), hy(18)), CG, (-40, 0))
flying((cx + 40, cy - 26), (hx(33), hy(16)), CG, (140, 40))
flying((cx + 80, cy - 26), (hx(35), hy(12)), C5, (260, 60))
txt(cx - 80, cy + 44, 'rot', 11, 'middle'); txt(cx - 40, cy + 44, 'schw.', 11, 'middle')
txt(cx + 40, cy + 44, 'schw.', 11, 'middle'); txt(cx + 80, cy + 44, 'gelb', 11, 'middle')

# ---------------- solder points ----------------
for x, y, c in [(1, 8, C12), (1, 9, CG), (1, 12, C12), (1, 13, CG), (3, 8, C12), (5, 8, C12), (7, 8, C12),
                (9, 8, C12), (10, 8, C12), (11, 8, C12), (12, 8, C12), (13, 8, C12), (12, 9, C12), (12, 10, CG),
                (10, 12, CG), (2, 9, CG), (2, 13, CG), (2, 16, CG), (2, 18, CG), (10, 18, CG), (11, 18, CG),
                (12, 18, CG), (13, 18, CG),
                (34, 1, C5), (34, 2, CG), (34, 3, C33), (34, 4, CTX), (34, 5, CRX),
                (35, 10, C5), (35, 12, C5), (33, 10, CG), (33, 14, CG), (33, 16, CG), (32, 10, C33), (31, 10, CTX),
                (30, 10, CRX), (25, 10, CRX), (28, 10, CRX), (27, 11, CTX), (31, 11, CTX), (32, 12, C33),
                (26, 12, C33), (28, 12, C33), (25, 14, C33), (26, 14, C33)]:
    solder(x, y, c)

label_hole(9, 8, '12 V abgesichert', -40, -12, C12, 12)
label_hole(4, 10.6, 'D1: Brücke oder 1N400x (Ring →)', 0, 0, '#555', 11)
label_hole(10, 12, 'D2 P6KE24A, Ring oben', -10, 22, '#333', 11)
label_hole(12, 10, 'C1 22 µF', 22, 4, '#333', 11)
label_hole(25, 12, 'R1 10 kΩ', -14, 4, '#333', 11, 'end')
label_hole(3, 19, 'Masse (Reihe 18)', 0, 4, CG, 12)
label_hole(28, 15.4, 'Masse ESP (33|10–16)', 0, 0, CG, 11)

# ---------------- connection list ----------------
LX, LY = LIST_X, 120
a(f'<rect x="{LX-20}" y="{LY-30}" width="660" height="{LIST_H}" rx="10" fill="#fafafa" stroke="#ddd"/>')
txt(LX, LY, 'Verdrahtungsliste', 18, bold=True)
rows = [
    ('12 V (links)', None),
    ('1', 'JST IN + (1|8) → JST OUT + (1|12) über (3|8)–(3|12)'),
    ('2', 'JST IN − (1|9) / OUT − (1|13) → Masse (2|9)…(2|18)…(13|18)'),
    ('3', '(3|8) → (5|8) → Sicherungshalter F1 1 A → (7|8)'),
    ('4', '(7|8) → (9|8): Drahtbrücke (oder 1N400x, Ring rechts)'),
    ('5', '12-V-Leitung (9|8) … (13|8)'),
    ('6', 'D2 P6KE24A: Ring (10|8), Ende (10|12) → Masse'),
    ('7', 'C1 22 µF: + (12|9), − (12|10) → Masse'),
    ('8', 'Wandler rot → (11|8), schwarz (Eingang) → (11|18)'),
    ('ESP32-C3 (rechte Leiste → Lötseite → Reihe 10)', None),
    ('9', '5V (34|1) → (35|10);  G (34|2) → (33|10)'),
    ('10', '3.3 (34|3) → (32|10);  4 (34|4) → (31|10);  3 (34|5) → (30|10)'),
    ('11', 'Wandler gelb → (35|12), schwarz (Ausgang) → (33|16)'),
    ('12', 'RX-Leitung (25|10)…(30|10) = GPIO3'),
    ('13', 'TX-Leitung (27|11)…(31|11)–(31|10) = GPIO4'),
    ('14', '3V3-Leitung (26|12)…(32|12)–(32|10)'),
    ('15', 'R1 10 kΩ (25|10)–(25|14), Brücke (25|14)–(26|14)–(26|12)'),
    ('LIN-Modul (Schraubklemmen)', None),
    ('16', 'oben RX → (28|10)   TX → (27|11)   SLP → (28|12)'),
    ('17', 'oben GND → (33|14)   (verbindet Modul- und ESP-Masse)'),
    ('18', 'unten 12V → (13|8)   GND → (13|18)   INH frei'),
    ('19', 'unten LIN → RJ12 IN Kontakt 3 (vorher messen)'),
    ('RJ12', None),
    ('20', 'RJ12 IN ↔ OUT alle 6 Kontakte 1:1; Kontakt 5 → (2|16)'),
    ('Vor dem ersten Anschluss', None),
    ('✓', 'Kein Durchgang 12 V ↔ Masse, 5 V ↔ Masse, 3V3 ↔ Masse'),
    ('✓', 'Durchgang Masse links (13|18) ↔ Masse rechts (33|14)'),
    ('✓', 'Modul: Widerstand 12V↔LIN ≥ 30 kΩ (kein Master-R)'),
    ('✓', '12 V anlegen: 5 V an (35|12), RX am Modul ≤ 3,3 V'),
    ('✓', 'Polarität am CP-Plus-Stecker messen, erst dann einstecken'),
]
y = LY + 34
for k, t in rows:
    if t is None:
        y += 6
        txt(LX, y, k, 14, bold=True, col='#2b5fae')
        y += 21
        continue
    txt(LX + 4, y, k, 13, bold=True)
    txt(LX + 36, y, t, 13)
    y += 22

a('</svg>')
svg = '\n'.join(s)
open(out + '.svg', 'w').write(svg)
cairosvg.svg2png(bytestring=svg.encode(), write_to=out + '.png')
