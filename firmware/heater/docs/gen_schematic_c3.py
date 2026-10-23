#!/usr/bin/env python3
"""Generate the schematic (Schaltplan) for the perfboard build as SVG + PNG."""
import sys
import cairosvg

out = sys.argv[1]
W, H = 1500, 980
s = []
a = s.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
  'font-family="DejaVu Sans, Arial, sans-serif">')
a(f'<rect width="{W}" height="{H}" fill="#fff"/>')
a('<text x="40" y="46" font-size="28" font-weight="bold">Schaltplan – Truma LIN-Slave (Lochraster)</text>')
a('<text x="40" y="74" font-size="15" fill="#555">ESP32-C3 Super Mini · LIN-Modul T151 (TJA1020) · Bauer DC/DC 8–32 V → 5 V · '
  'CP Plus bleibt Master</text>')

C12, C5, C33, CG, CL, CTX, CRX = '#d62728', '#ff7f0e', '#c2185b', '#111', '#1f5fbf', '#2ca02c', '#17becf'


def line(pts, col, w=3):
    p = ' '.join(f'{x},{y}' for x, y in pts)
    a(f'<polyline points="{p}" fill="none" stroke="{col}" stroke-width="{w}" stroke-linejoin="round"/>')


def dot(x, y, col):
    a(f'<circle cx="{x}" cy="{y}" r="5" fill="{col}"/>')


def txt(x, y, t, size=14, anchor='start', col='#111', bold=False):
    b = ' font-weight="bold"' if bold else ''
    a(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" fill="{col}"{b}>{t}</text>')


def box(x, y, w, h, title, fill='#f6f6f6', stroke='#333', sub=None):
    a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    txt(x + w / 2, y - 10, title, 15, 'middle', bold=True)
    if sub:
        txt(x + w / 2, y + h + 18, sub, 12, 'middle', '#555')


def gnd(x, y):
    a(f'<g stroke="{CG}" stroke-width="3"><line x1="{x}" y1="{y}" x2="{x}" y2="{y+12}"/>'
      f'<line x1="{x-14}" y1="{y+12}" x2="{x+14}" y2="{y+12}"/><line x1="{x-9}" y1="{y+18}" x2="{x+9}" y2="{y+18}"/>'
      f'<line x1="{x-4}" y1="{y+24}" x2="{x+4}" y2="{y+24}"/></g>')


# ---------------- connectors (left) ----------------
# JST 12 V pass-through
box(40, 140, 150, 110, 'JST IN (Kabelbaum)', '#fdecea', C12)
txt(180, 180, '+ 12 V', 14, 'end'); txt(180, 225, '− GND', 14, 'end')
box(40, 330, 150, 110, 'JST OUT (zum CP Plus)', '#fdecea', C12)
txt(180, 370, '+ 12 V', 14, 'end'); txt(180, 415, '− GND', 14, 'end')
line([(190, 175), (250, 175), (250, 365), (190, 365)], C12, 4)
line([(190, 220), (225, 220), (225, 410), (190, 410)], CG, 4)
dot(250, 175, C12); dot(225, 410, CG)
txt(258, 280, '1:1 durch', 12, col='#666')

# RJ12 pass-through
box(40, 560, 150, 160, 'RJ12 IN (Heizung)', '#e8f0fb', CL)
box(40, 790, 150, 160, 'RJ12 OUT (CP Plus)', '#e8f0fb', CL)
for i in range(6):
    y1 = 585 + i * 22
    y2 = 815 + i * 22
    txt(182, y1 + 5, f'{i+1}', 12, 'end'); txt(182, y2 + 5, f'{i+1}', 12, 'end')
    col = CL if i == 2 else (CG if i == 4 else '#999')
    xx = 205 + i * 9
    line([(190, y1), (xx, y1), (xx, y2), (190, y2)], col, 3 if i in (2, 4) else 2)
txt(320, 700, 'alle 6 Kontakte 1:1', 12, col='#666')
txt(320, 716, 'LIN = Pin 3, GND = Pin 5', 12, col='#666')
txt(320, 732, '(vorher messen!)', 12, col='#b00')

# ---------------- 12 V branch ----------------
# fuse
line([(250, 175), (330, 175)], C12, 4)
a('<rect x="330" y="160" width="80" height="30" rx="5" fill="#fff" stroke="#333" stroke-width="2"/>')
txt(370, 181, 'F1', 15, 'middle', bold=True)
txt(370, 215, 'Flachsicherung 1 A', 12, 'middle', '#555')
txt(370, 230, '(fliegender Halter)', 12, 'middle', '#555')
# optional D1
line([(410, 175), (470, 175)], C12, 4)
a('<rect x="470" y="160" width="80" height="30" rx="5" fill="#fff" stroke="#888" stroke-width="2" stroke-dasharray="6,4"/>')
txt(510, 181, 'D1 ▶|', 14, 'middle', '#666', True)
txt(510, 215, 'optional 1N400x', 12, 'middle', '#555')
txt(510, 230, 'sonst Drahtbrücke', 12, 'middle', '#555')
line([(550, 175), (980, 175)], C12, 4)
txt(600, 160, '+12 V (abgesichert)', 13, col=C12)
# D2 TVS
dot(620, 175, C12)
line([(620, 175), (620, 240)], C12, 3)
a('<rect x="595" y="240" width="50" height="60" rx="5" fill="#fff" stroke="#333" stroke-width="2"/>')
a('<rect x="595" y="240" width="50" height="10" fill="#333"/>')
txt(620, 280, 'D2', 14, 'middle', bold=True)
txt(660, 265, 'P6KE24A', 12, col='#555'); txt(660, 280, 'Ring oben (+)', 12, col='#555')
line([(620, 300), (620, 330)], CG, 3); gnd(620, 330)
# C1
dot(760, 175, C12)
line([(760, 175), (760, 245)], C12, 3)
a('<line x1="735" y1="250" x2="785" y2="250" stroke="#333" stroke-width="4"/>')
a('<path d="M735,268 Q760,256 785,268" fill="none" stroke="#333" stroke-width="4"/>')
txt(728, 248, '+', 16, 'end', bold=True)
txt(795, 255, 'C1', 14, bold=True); txt(795, 272, '22 µF / 35 V', 12, col='#555')
line([(760, 262), (760, 330)], CG, 3); gnd(760, 330)

# DC/DC converter
box(980, 130, 200, 120, 'DC/DC Bauer 8–32 V → 5 V', '#fff4e5', C5)
txt(990, 180, 'IN+ rot', 13); txt(990, 228, 'IN− schwarz', 13)
txt(1170, 180, 'OUT+ gelb', 13, 'end'); txt(1170, 228, 'OUT− schw.', 13, 'end')
line([(980, 225), (950, 225), (950, 330)], CG, 3); gnd(950, 330)
line([(1180, 175), (1215, 175), (1215, 470), (1230, 470)], C5, 4)
line([(1180, 225), (1195, 225), (1195, 232)], CG, 3); gnd(1195, 232)
txt(1222, 200, '5 V', 14, col=C5, bold=True)

# ---------------- ESP32 ----------------
box(1230, 230, 230, 330, 'ESP32-C3 Super Mini', '#eef4ff', CL, 'rechte Stiftleiste (Draufsicht, USB oben)')
pins = [('3V3', '„3.3“', 270), ('GPIO4 (TX)', '„4“', 350), ('GPIO3 (RX)', '„3“', 390),
        ('5V', '„5V“', 470), ('GND', '„G“', 520)]
for name, pin, y in pins:
    txt(1245, y + 5, name, 14)
    txt(1450, y + 5, pin, 12, 'end', '#666')
line([(1230, 520), (1200, 520), (1200, 560)], CG, 3); gnd(1200, 560)

# ---------------- LIN module ----------------
box(560, 520, 320, 300, 'LIN-Modul T151 (TJA1020)', '#f3f3f3', '#333', 'Master-Widerstand vorher prüfen (12V↔LIN)')
left = [('12V', 570), ('LIN', 630), ('INH (frei)', 690), ('GND', 750)]
right = [('TX', 570), ('RX', 630), ('SLP', 690), ('GND', 750)]
for n, y in left:
    a(f'<circle cx="560" cy="{y}" r="7" fill="#fff" stroke="#333" stroke-width="2"/>')
    txt(575, y + 5, n, 14, col='#888' if 'frei' in n else '#111')
for n, y in right:
    a(f'<circle cx="880" cy="{y}" r="7" fill="#fff" stroke="#333" stroke-width="2"/>')
    txt(865, y + 5, n, 14, 'end')
# 12V to module
dot(575, 175, C12)
line([(575, 175), (575, 470), (500, 470), (500, 570), (553, 570)], C12, 3)
# LIN from RJ12 pin 3 (IN side x=223)
dot(223, 629, CL)
line([(223, 629), (223, 630), (553, 630)], CL, 4)
txt(330, 622, 'LIN (Pin 3)', 13, col=CL)
# module GND bus side
line([(553, 750), (520, 750), (520, 860)], CG, 3); gnd(520, 860)
# RJ12 GND pin 5 (x=241)
dot(241, 673, CG)
line([(241, 673), (241, 673), (300, 673), (300, 860)], CG, 3); gnd(300, 860)
txt(308, 668, 'GND (Pin 5)', 13)
# TX/RX/SLP/GND to ESP
line([(887, 570), (1100, 570), (1100, 350), (1230, 350)], CTX, 3)
line([(887, 630), (1130, 630), (1130, 390), (1230, 390)], CRX, 3)
# 3V3 rail
line([(1230, 270), (1060, 270), (1060, 690), (887, 690)], C33, 3)
dot(1060, 270, C33)
# R1 pull-up 3V3 -> RX
dot(1060, 600, C33); dot(1130, 600, CRX)
line([(1060, 600), (1072, 600)], C33, 3); line([(1118, 600), (1130, 600)], CRX, 3)
a('<rect x="1072" y="590" width="46" height="20" rx="3" fill="#fff" stroke="#333" stroke-width="2"/>')
txt(1095, 585, 'R1 10 kΩ', 12, 'middle', bold=True)
txt(1066, 290, '3V3', 13, col=C33, bold=True)
line([(887, 750), (960, 750), (960, 860)], CG, 3); gnd(960, 860)

# notes
notes = [
    '• Gemeinsame Masse: alle Masse-Symbole sind verbunden (12-V-Minus, Modul, Wandler, ESP GND, RJ12 Pin 5).',
    '• Vor dem ersten Anschluss: Polarität am JST messen, Master-Widerstand am Modul prüfen, RX am Modul ≤ 3,3 V.',
    '• Firmware: BOARD_ESP32C3_SUPERMINI (TX = GPIO4, RX = GPIO3). Kreuzungen ohne Punkt: nicht verbunden.',
]
for i, n in enumerate(notes):
    txt(560, 900 + i * 22, n.replace('⏚', 'Masse-Symbole'), 14, col='#333')
a('</svg>')
svg = '\n'.join(s)
open(out + '.svg', 'w').write(svg)
cairosvg.svg2png(bytestring=svg.encode(), write_to=out + '.png')
