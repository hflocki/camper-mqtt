"""Draws the schematic of the camper LIN board (schaltplan.pdf / .svg)."""
import sys
import schemdraw
import schemdraw.elements as elm

schemdraw.config(fontsize=9, lw=1)
OUT = sys.argv[1] if len(sys.argv) > 1 else 'schaltplan'
NET = '#b03000'


def pin(name, side, num='', a=None):
    return elm.IcPin(name=name, side=side, pin=num, anchorname=a or name)


def tag(d, at, text, direction='right', length=0.8):
    line = elm.Line().at(at)
    line = line.right(length) if direction == 'right' else line.left(length)
    d.add(line)
    d.add(elm.Label().at(d.here).label(text, loc='right' if direction == 'right' else 'left', color=NET))


def gnd(d, at):
    d.add(elm.Ground().at(at))


with schemdraw.Drawing(show=False) as d:
    d.config(unit=2.4)
    d.add(elm.Label().at((0, 3)).label('camper-heater LIN v1.0 – Schaltplan', loc='right', fontsize=14))

    # ===== 12 V input =====
    d.add(elm.Label().at((0, 1.4)).label('12-V-Eingang: Sicherung, Verpolschutz, Überspannungsschutz, Wandler', loc='right', fontsize=11))
    d.add(elm.Dot(open=True).at((0, 0)).label('J1-1 +12 V', loc='left'))
    d.add(elm.Fuse().right().label('F1 1 A'))
    d.add(elm.Schottky().right().label('D1 B1100'))
    d.add(elm.Dot().label('VBAT', loc='top', color=NET))
    d.push(); d.add(elm.Line().right(0.6)); z = d.add(elm.Zener().down().reverse().label('D2\nSMBJ22A', loc='bottom', ofst=0.3)); gnd(d, z.end); d.pop()
    d.add(elm.Line().right(2.2))
    d.push(); c = d.add(elm.Capacitor().down().label('C1 10µ\nC2 100n', loc='bottom', ofst=0.3)); gnd(d, c.end); d.pop()
    d.add(elm.Line().right(1.8))
    ps = elm.Ic(pins=[pin('IN', 'L', '1'), pin('OUT', 'R', '3'), pin('GND', 'B', '2')], size=(3, 1.6), pinspacing=1,
                label='PS1 R-78HB5.0-0.5', lblofst=0.3, lblloc='top')
    d.add(ps.right().anchor('IN')); gnd(d, ps.GND)
    d.add(elm.Line().at(ps.OUT).right(0.8))
    d.add(elm.Dot().label('+5V_DC', loc='top', color=NET))
    d.push(); c = d.add(elm.Capacitor().down().label('C3 22µ', loc='bottom', ofst=0.3)); gnd(d, c.end); d.pop()
    d.add(elm.Line().right(0.6)); d.add(elm.Schottky().right().label('D3 B5819W'))
    d.add(elm.Dot().label('+5V', loc='top', color=NET)); p5 = d.here
    d.push(); d.add(elm.Line().down(2.8)); d.add(elm.Schottky().left().reverse().label('D4 B5819W', loc='bottom'))
    d.add(elm.Line().left(0.4)); d.add(elm.Label().at(d.here).label('VBUS (USB)', loc='left', color=NET)); d.pop()
    d.add(elm.Line().right(0.8))
    d.push(); c = d.add(elm.Capacitor().down().label('C4 10µ', loc='bottom', ofst=0.3)); gnd(d, c.end); d.pop()
    d.add(elm.Line().right(1.2))
    u3 = elm.Ic(pins=[pin('VIN', 'L', '1'), pin('EN', 'L', '3'), pin('VOUT', 'R', '5'), pin('GND', 'B', '2')], size=(3, 2.4), pinspacing=1,
                label='U3 AP2112K-3.3', lblofst=0.3, lblloc='top')
    d.add(u3.right().anchor('VIN')); gnd(d, u3.GND)
    d.add(elm.Line().at(u3.EN).left(0.5)); d.add(elm.Line().up(0.8)); d.add(elm.Label().at(d.here).label('+5V', loc='top', color=NET))
    d.add(elm.Line().at(u3.VOUT).right(0.8)); d.add(elm.Dot().label('+3V3', loc='top', color=NET))
    d.push(); c = d.add(elm.Capacitor().down().label('C5 22µ\nC6 100n', loc='bottom', ofst=0.3)); gnd(d, c.end); d.pop()
    d.add(elm.Line().right(2.0)); d.add(elm.Resistor().down().label('R11 1k', loc='bottom', ofst=0.3))
    led = d.add(elm.LED().down().label('LED1 grün', loc='bottom', ofst=0.3)); gnd(d, led.end)
    d.add(elm.Label().at((0, -1.6)).label('J1-2 = GND', loc='right', fontsize=9))

    # ===== ESP32-C3 =====
    y = -12
    d.add(elm.Label().at((0, y + 5.2)).label('ESP32-C3-WROOM-02', loc='right', fontsize=11))
    u1 = elm.Ic(pins=[pin('3V3', 'L', '1'), pin('EN', 'L', '2'), pin('IO9', 'L', '8'), pin('IO8', 'L', '7'), pin('IO2', 'L', '16'),
                      pin('GND', 'L', '9,19'),
                      pin('IO4', 'R', '3'), pin('IO3', 'R', '15'), pin('IO5', 'R', '4'), pin('IO6', 'R', '5'), pin('IO7', 'R', '6'),
                      pin('IO18', 'R', '13'), pin('IO19', 'R', '14')],
                size=(3.2, 7.6), pinspacing=1.05, label='U1', lblloc='top', lblofst=0.3)
    d.add(u1.right().at((14, y - 3)).anchor('center'))
    for n, t in [('3V3', '+3V3 · C7 22µ · C8 100n'), ('EN', 'R1 10k → +3V3 · C9 1µ → GND · SW1 RESET → GND'),
                 ('IO9', 'R2 10k → +3V3 · SW2 BOOT → GND'), ('IO8', 'R3 10k → +3V3'), ('IO2', 'R4 10k → +3V3'), ('GND', 'GND')]:
        tag(d, getattr(u1, n), t, 'left')
    for n, t in [('IO4', 'LIN_TX'), ('IO3', 'LIN_RX'), ('IO5', 'LIN_SLP'), ('IO6', 'R12 1k → LED2 blau (LIN)'),
                 ('IO7', 'R13 1k → LED3 gelb (MQTT)'), ('IO18', 'USB_DN'), ('IO19', 'USB_DP')]:
        tag(d, getattr(u1, n), t, 'right')

    # ===== LIN =====
    x = 36
    d.add(elm.Label().at((x - 6, y + 5.2)).label('LIN (Slave wie iNet Box, CP Plus bleibt Master)', loc='right', fontsize=11))
    u2 = elm.Ic(pins=[pin('RXD', 'L', '1'), pin('SLP', 'L', '2'), pin('WAKE', 'L', '3'), pin('TXD', 'L', '4'),
                      pin('INH', 'R', '8'), pin('BAT', 'R', '7'), pin('LIN', 'R', '6'), pin('GND', 'R', '5')],
                size=(3, 4.4), pinspacing=1.05, label='U2 TJA1021T/20', lblloc='top', lblofst=0.3)
    d.add(u2.right().at((x, y - 1.5)).anchor('center'))
    for n, t in [('RXD', 'LIN_RX · R5 4,7k → +3V3'), ('SLP', 'LIN_SLP · R7 10k → +3V3'), ('WAKE', 'R8 10k → VBAT'),
                 ('TXD', 'LIN_TX · R6 10k → +3V3')]:
        tag(d, getattr(u2, n), t, 'left')
    tag(d, u2.INH, 'offen'); tag(d, u2.BAT, 'VBAT · C10 100n'); tag(d, u2.GND, 'GND')
    d.add(elm.Line().at(u2.LIN).right(1.6)); d.add(elm.Dot().label('LIN', loc='top', color=NET)); plin = d.here
    d.push(); c = d.add(elm.Capacitor().down().label('C11 220p', loc='top', ofst=0.3)); gnd(d, c.end); d.pop()
    d.add(elm.Line().right(2.4))
    rj = elm.Ic(pins=[pin('1', 'L', '', 'p1'), pin('2', 'L', '', 'p2'), pin('3', 'L', '', 'p3'), pin('4', 'L', '', 'p4'),
                      pin('5', 'L', '', 'p5'), pin('6', 'L', '', 'p6')],
                size=(3, 5.6), pinspacing=0.8, label='J2 = J3\nRJ12 6P6C\n1:1 verbunden', lblloc='center')
    d.add(rj.right().anchor('p3'))
    d.add(elm.Line().at(rj.p5).left(1.6)); d.add(elm.Line().down(3.4))
    d.add(elm.Switch().down().label('JP1 offen', loc='bottom', ofst=0.3)); gnd(d, d.here)
    d.add(elm.Label().at((x - 6, y - 8.2)).label('Pin 3 = LIN, Pin 5 = Masse (wie havanti). Pin 1, 2, 4, 6 gehen nur von J2 nach J3 durch.', loc='right', fontsize=9))
    d.add(elm.Label().at((x - 6, y - 9.2)).label('JP1 erst schließen, wenn Pin 5 nachgemessen Masse ist (Masse kommt sonst über J1).', loc='right', fontsize=9))

    # ===== USB =====
    y2 = -26
    d.add(elm.Label().at((0, y2 + 4.5)).label('USB-C: Flashen, Log, Strom ohne 12 V', loc='right', fontsize=11))
    j4 = elm.Ic(pins=[pin('VBUS', 'R', 'A4 A9 B4 B9'), pin('D+', 'R', 'A6 B6', 'DP'), pin('D−', 'R', 'A7 B7', 'DN'),
                      pin('CC1', 'R', 'A5'), pin('CC2', 'R', 'B5'), pin('GND', 'R', 'A1 A12 B1 B12 S', 'G')],
                size=(3, 6), pinspacing=1.0, label='J4 USB-C', lblloc='top', lblofst=0.3)
    d.add(j4.right().at((3, y2 - 1)).anchor('center'))
    tag(d, j4.VBUS, 'VBUS → D4, U4-5', 'right', 2.2); tag(d, j4.DP, 'USB_DP → U4-3/4', 'right', 2.2)
    tag(d, j4.DN, 'USB_DN → U4-1/6', 'right', 2.2); tag(d, j4.CC1, 'R9 5,1k → GND', 'right', 2.2)
    tag(d, j4.CC2, 'R10 5,1k → GND', 'right', 2.2); tag(d, j4.G, 'GND', 'right', 2.2)
    u4 = elm.Ic(pins=[pin('IO1', 'L', '1'), pin('GND', 'L', '2'), pin('IO2', 'L', '3'), pin('IO2b', 'R', '4', 'IO2B'),
                      pin('VBUS', 'R', '5'), pin('IO1b', 'R', '6', 'IO1B')],
                size=(3, 3.2), pinspacing=1.0, label='U4 USBLC6-2SC6', lblloc='top', lblofst=0.3)
    d.add(u4.right().at((20, y2 - 1)).anchor('center'))
    tag(d, u4.IO1, 'USB_DN', 'left'); tag(d, u4.GND, 'GND', 'left'); tag(d, u4.IO2, 'USB_DP', 'left')
    tag(d, u4.IO2B, 'USB_DP'); tag(d, u4.VBUS, 'VBUS'); tag(d, u4.IO1B, 'USB_DN')
    d.add(elm.Label().at((30, y2 + 1.5)).label('Messpunkte: TP1 VBAT · TP2 +5V · TP3 +3V3 · TP4 GND · TP5 LIN', loc='right', fontsize=9))
    d.add(elm.Label().at((30, y2 + 0.5)).label('4 × Befestigungsloch M3 (H1–H4)', loc='right', fontsize=9))
    d.add(elm.Label().at((30, y2 - 0.5)).label('Firmware: camper-heater, Board-Profil BOARD_CAMPER_LIN_C3', loc='right', fontsize=9))
    d.add(elm.Label().at((30, y2 - 1.5)).label('GPIO4 = LIN TX · GPIO3 = LIN RX · GPIO5 = SLP · GPIO6/7 = LEDs', loc='right', fontsize=9))
    d.save(OUT + '.svg'); d.save(OUT + '.pdf')
print('ok')
