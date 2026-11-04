"""Emulator core: field table, payload builders, commands and scenarios.

No networking in here. emulator.py owns the broker connections and the web server and
asks this module what to send. Every value and setting of every emulated device is one
field; the web page is built from this table (GET /api/emu/schema), the state file stores
the values by key and the builders read them by key. Values are kept as text: numbers as
typed, bools as ON / OFF, enums as one of the options.

Ported from the former ESP32 firmware (firmware/emulator), same topics and formats.
"""
import json
import math
import random
import threading
import time
from collections import deque
from pathlib import Path

# ============================================================
# Devices
# ============================================================

D_TANK, D_LEVEL, D_HEATER, D_FAN, D_TEMP, D_BATTERY, D_ROUTER, D_ZIGBEE, D_TOOLBOX = range(9)

# id, German name for the web page, field holding the base topic, own connection with status + last will
DEVS = [
    ("tanklevel", "Tanks", "tk_pre", True),
    ("level", "Levelsensor", "lv_pre", True),
    ("heater", "Heizung", "ht_pre", True),
    ("maxxfan", "MaxxFan", "mf_pre", True),
    ("temp", "Temperaturen", "tp_pre", True),
    ("battery", "Batterien", "bt_pre", True),
    ("router", "Router", "", False),      # sent over the emulator's own connection
    ("zigbee", "Zigbee", "", False),
    ("toolbox", "Toolbox (Dachhaube)", "tb_pre", True),
]
D_COUNT = len(DEVS)

M_OFF, M_ON, M_OFFLINE = 0, 1, 2   # nothing sent / online / connection dropped (router, Zigbee: paused)

# ============================================================
# Field table
# ============================================================

FL_SET, FL_TOPIC, FL_OPT, FL_ADV = 1, 2, 4, 8   # setting, topic, may be empty, shown under "more"


def NUM(k, d, g, fl, lbl, u, mn, mx, st, dc, nz, df):
    return dict(key=k, dev=d, group=g, type="num", flags=fl, label=lbl, unit=u, min=mn, max=mx,
                step=st, dec=dc, noise=nz, defv=df, opts="")


def TXT(k, d, g, fl, lbl, df):
    return dict(key=k, dev=d, group=g, type="text", flags=fl, label=lbl, unit="", min=0, max=0,
                step=0, dec=0, noise=0, defv=df, opts="")


def BOOL(k, d, g, fl, lbl, df):
    return dict(TXT(k, d, g, fl, lbl, df), type="bool")


def ENUM(k, d, g, fl, lbl, df, op):
    return dict(TXT(k, d, g, fl, lbl, df), type="enum", opts=op)


def temp_slot(n, on, sid, name, t, lo, hi):
    g = f"Sensor {n}"
    return [
        BOOL(f"t{n}_on", D_TEMP, g, 0, "Sensor vorhanden", on),
        TXT(f"t{n}_id", D_TEMP, g, FL_SET | FL_TOPIC, "ID (Topic)", sid),
        TXT(f"t{n}_name", D_TEMP, g, FL_SET, "Name", name),
        NUM(f"t{n}_t", D_TEMP, g, 0, "Temperatur", "°C", -40, 60, 0.1, 2, 0.1, t),
        BOOL(f"t{n}_ok", D_TEMP, g, 0, "Sensor antwortet", "ON"),
        NUM(f"t{n}_lo", D_TEMP, g, FL_SET | FL_OPT, "Alarm unter", "°C", -40, 60, 0.5, 1, 0, lo),
        NUM(f"t{n}_hi", D_TEMP, g, FL_SET | FL_OPT, "Alarm über", "°C", -40, 60, 0.5, 1, 0, hi),
    ]


def bat_slot(n, on, bid, name, btype, soc, v, a, cap):
    g = f"Batterie {n}"
    return [
        BOOL(f"b{n}_on", D_BATTERY, g, 0, "Batterie vorhanden", on),
        TXT(f"b{n}_id", D_BATTERY, g, FL_SET | FL_TOPIC, "ID (Topic)", bid),
        TXT(f"b{n}_name", D_BATTERY, g, FL_SET, "Name", name),
        ENUM(f"b{n}_type", D_BATTERY, g, FL_SET, "Typ", btype, "1=Daly,2=Ective,3=BM2 (ohne Strom)"),
        BOOL(f"b{n}_ok", D_BATTERY, g, 0, "BMS antwortet", "ON"),
        NUM(f"b{n}_soc", D_BATTERY, g, 0, "Ladezustand", "%", 0, 100, 1, 0, 0, soc),
        NUM(f"b{n}_v", D_BATTERY, g, 0, "Spannung", "V", 10, 15, 0.01, 2, 0.01, v),
        NUM(f"b{n}_a", D_BATTERY, g, 0, "Strom (+ = Laden)", "A", -100, 100, 0.1, 2, 0.2, a),
        NUM(f"b{n}_cap", D_BATTERY, g, FL_SET, "Kapazität", "Ah", 10, 600, 10, 0, 0, cap),
        NUM(f"b{n}_temp", D_BATTERY, g, FL_ADV, "Temperatur (Daly)", "°C", -20, 60, 0.5, 1, 0, "18"),
        NUM(f"b{n}_cyc", D_BATTERY, g, FL_ADV, "Ladezyklen (Daly)", "", 0, 9999, 1, 0, 0, "42"),
        BOOL(f"b{n}_prob", D_BATTERY, g, FL_ADV, "BMS meldet Fehler", "OFF"),
        TXT(f"b{n}_pt", D_BATTERY, g, FL_ADV, "Fehlertext", "Zellspannung zu niedrig"),
    ]


def zb_contact(n, on, ieee, name, is_open):
    g = f"Kontakt {n}"
    return [
        BOOL(f"c{n}_on", D_ZIGBEE, g, 0, "Kontakt vorhanden", on),
        TXT(f"c{n}_ieee", D_ZIGBEE, g, FL_SET | FL_TOPIC, "IEEE-Adresse", ieee),
        TXT(f"c{n}_name", D_ZIGBEE, g, FL_SET, "Name (nur hier)", name),
        BOOL(f"c{n}_open", D_ZIGBEE, g, 0, "Offen", is_open),
        NUM(f"c{n}_bat", D_ZIGBEE, g, FL_ADV, "Batterie", "%", 0, 100, 1, 0, 0, "90"),
    ]


def zb_climate(n, on, ieee, name, t, h):
    g = f"Klimasensor {n}"
    return [
        BOOL(f"k{n}_on", D_ZIGBEE, g, 0, "Sensor vorhanden", on),
        TXT(f"k{n}_ieee", D_ZIGBEE, g, FL_SET | FL_TOPIC, "IEEE-Adresse", ieee),
        TXT(f"k{n}_name", D_ZIGBEE, g, FL_SET, "Name (nur hier)", name),
        NUM(f"k{n}_t", D_ZIGBEE, g, 0, "Temperatur", "°C", -40, 60, 0.1, 2, 0.1, t),
        NUM(f"k{n}_h", D_ZIGBEE, g, 0, "Luftfeuchte", "%", 0, 100, 1, 1, 0.5, h),
        NUM(f"k{n}_bat", D_ZIGBEE, g, FL_ADV, "Batterie", "%", 0, 100, 1, 0, 0, "85"),
    ]


FIELDS = [
    # ---- tanks (camper-tanklevel) ----
    TXT("tk_pre", D_TANK, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/tanklevel"),
    NUM("w_pct", D_TANK, "Frischwasser", 0, "Füllstand", "%", 0, 100, 1, 0, 0.6, "62"),
    NUM("w_cap", D_TANK, "Frischwasser", FL_SET, "Tankgröße", "l", 10, 300, 5, 0, 0, "100"),
    BOOL("w_filter", D_TANK, "Frischwasser", FL_ADV, "Glättung aktiv", "ON"),
    BOOL("g_on", D_TANK, "Grauwasser", 0, "Grauwasser eingeschaltet", "ON"),
    NUM("g_pct", D_TANK, "Grauwasser", 0, "Füllstand", "%", 0, 100, 1, 0, 0.6, "35"),
    NUM("g_cap", D_TANK, "Grauwasser", FL_SET, "Tankgröße", "l", 10, 300, 5, 0, 0, "80"),
    BOOL("g_filter", D_TANK, "Grauwasser", FL_ADV, "Glättung aktiv", "ON"),
    NUM("gas_pct", D_TANK, "Gas", 0, "Füllstand", "%", 0, 100, 1, 0, 0.4, "71"),
    NUM("gas_kg", D_TANK, "Gas", FL_SET, "Flasche (Füllmenge)", "kg", 1, 33, 0.5, 1, 0, "11"),
    ENUM("gas_state", D_TANK, "Gas", 0, "Sensor", "OK", "OK,Kein Signal,Sensorfehler"),
    NUM("gas_bat", D_TANK, "Gas", FL_ADV, "Akku Sensor", "%", 0, 100, 1, 0, 0, "87"),
    NUM("gas_rssi", D_TANK, "Gas", FL_ADV, "Bluetooth-Signal", "dBm", -110, -30, 1, 0, 3, "-72"),

    # ---- level sensor (camper-level) ----
    TXT("lv_pre", D_LEVEL, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/level"),
    NUM("pitch", D_LEVEL, "", 0, "Längsneigung (vorne höher +)", "°", -15, 15, 0.1, 1, 0.05, "0.4"),
    NUM("roll", D_LEVEL, "", 0, "Querneigung (links höher +)", "°", -15, 15, 0.1, 1, 0.05, "-0.3"),
    NUM("tol", D_LEVEL, "", FL_SET, "Toleranz", "°", 0.1, 5, 0.1, 1, 0, "0.5"),
    ENUM("lv_sensor", D_LEVEL, "", 0, "Sensor", "OK", "OK,ERROR"),
    BOOL("lv_tare", D_LEVEL, "", FL_ADV, "Nullpunkt gesetzt", "OFF"),
    NUM("wb", D_LEVEL, "Fahrzeug", FL_SET | FL_ADV, "Radstand", "m", 2, 6, 0.05, 2, 0, "3.5"),
    NUM("tw", D_LEVEL, "Fahrzeug", FL_SET | FL_ADV, "Spurweite", "m", 1.2, 2.4, 0.05, 2, 0, "1.7"),

    # ---- heater (camper-heater, Truma) ----
    TXT("ht_pre", D_HEATER, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/heater"),
    BOOL("alive", D_HEATER, "", 0, "CP Plus verbunden", "ON"),
    ENUM("hvac", D_HEATER, "Raum", 0, "Raumheizung", "heat", "heat=An,off=Aus"),
    NUM("room", D_HEATER, "Raum", 0, "Raumtemperatur", "°C", -10, 40, 0.1, 1, 0.1, "19.5"),
    NUM("setp", D_HEATER, "Raum", 0, "Solltemperatur", "°C", 5, 30, 0.5, 1, 0, "21"),
    ENUM("fan", D_HEATER, "Raum", 0, "Lüfterstufe", "eco", "eco,high,boost"),
    BOOL("ht_dyn", D_HEATER, "Raum", FL_SET, "Raumtemperatur folgt der Heizung", "ON"),
    ENUM("wmode", D_HEATER, "Warmwasser", 0, "Warmwasser", "off", "off=Aus,eco=Eco 40 °C,high=Hot 60 °C,boost=Boost"),
    NUM("water", D_HEATER, "Warmwasser", 0, "Wassertemperatur", "°C", 0, 80, 1, 1, 0, "38"),
    NUM("ht_err", D_HEATER, "Diagnose", 0, "Fehlercode (0 = keiner)", "", 0, 999, 1, 0, 0, "0"),
    BOOL("decoke", D_HEATER, "Diagnose", FL_ADV, "Freibrennen", "OFF"),
    NUM("decoke_rem", D_HEATER, "Diagnose", FL_ADV, "Freibrennen Rest", "min", 0, 120, 1, 0, 0, "0"),

    # ---- roof fan (camper-maxxfan) ----
    TXT("mf_pre", D_FAN, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/maxxfan"),
    BOOL("mf_exp", D_FAN, "", FL_ADV, "Port-Expander erreichbar", "ON"),
    BOOL("p1_on", D_FAN, "Anschluss p1", FL_SET, "Lüfter angeschlossen", "ON"),
    NUM("p1_speed", D_FAN, "Anschluss p1", 0, "Stufe (nur hier, 0 = Haube zu)", "", 0, 10, 1, 0, 0, "0"),
    BOOL("p1_hold", D_FAN, "Anschluss p1", 0, "Auto-Halten", "OFF"),
    BOOL("p1_led", D_FAN, "Anschluss p1", 0, "LED", "OFF"),
    BOOL("p2_on", D_FAN, "Anschluss p2", FL_SET, "Lüfter angeschlossen", "OFF"),
    NUM("p2_speed", D_FAN, "Anschluss p2", 0, "Stufe (nur hier, 0 = Haube zu)", "", 0, 10, 1, 0, 0, "0"),
    BOOL("p2_hold", D_FAN, "Anschluss p2", 0, "Auto-Halten", "OFF"),
    BOOL("p2_led", D_FAN, "Anschluss p2", 0, "LED", "OFF"),

    # ---- temperatures (camper-temp) ----
    TXT("tp_pre", D_TEMP, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/temp"),
    *temp_slot(1, "ON", "fridge", "Kühlschrank", "5.0", "", "8"),
    *temp_slot(2, "ON", "freezer", "Gefrierfach", "-17.0", "", "-12"),
    *temp_slot(3, "OFF", "interior", "Innenraum", "20.0", "5", ""),
    *temp_slot(4, "OFF", "outside", "Außen", "8.0", "", ""),

    # ---- batteries (camper-battery) ----
    TXT("bt_pre", D_BATTERY, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/battery"),
    *bat_slot(1, "ON", "house", "Bordbatterie", "2", "78", "13.25", "-4.2", "100"),
    *bat_slot(2, "OFF", "starter", "Starterbatterie", "3", "90", "12.70", "0", "70"),

    # ---- toolbox (camper-toolbox, roof hatch over the plugin rvtech) ----
    TXT("tb_pre", D_TOOLBOX, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/toolbox"),
    BOOL("rv_on", D_TOOLBOX, "Dachhaube (Heki)", FL_SET, "Dachhaube gekoppelt", "ON"),
    BOOL("rv_conn", D_TOOLBOX, "Dachhaube (Heki)", 0, "Bluetooth verbunden", "ON"),
    NUM("rv_pos", D_TOOLBOX, "Dachhaube (Heki)", 0, "Position (102 = Lüften)", "", 0, 102, 1, 0, 0, "0"),
    NUM("rv_tgt", D_TOOLBOX, "Dachhaube (Heki)", FL_ADV, "Ziel (fährt dorthin)", "", 0, 102, 1, 0, 0, "0"),
    NUM("rv_speed", D_TOOLBOX, "Dachhaube (Heki)", FL_SET | FL_ADV, "Geschwindigkeit", "%/s", 1, 50, 1, 0, 0, "5"),

    # ---- router (RUT956 "Data to Server") ----
    TXT("rut_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "GPS (JSON)", "camper/rut"),
    TXT("door_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "Tür (Digital-Eingang)", "camper/door"),
    TXT("mob_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "Mobilfunk", "camper/router/mobile"),
    TXT("awn_t", D_ROUTER, "Topics", FL_SET | FL_ADV, "Markisen-Befehl des Displays (Szenario Abfahrt)", "camper/display/cmd/awning"),
    NUM("spd", D_ROUTER, "GPS", 0, "Geschwindigkeit", "km/h", 0, 160, 1, 1, 0, "0"),
    BOOL("fix", D_ROUTER, "GPS", 0, "GPS-Fix", "ON"),
    NUM("lat", D_ROUTER, "GPS", FL_ADV, "Breite", "°", -90, 90, 0.0001, 6, 0, "51.000000"),
    NUM("lon", D_ROUTER, "GPS", FL_ADV, "Länge", "°", -180, 180, 0.0001, 6, 0, "10.000000"),
    NUM("alt", D_ROUTER, "GPS", FL_ADV, "Höhe", "m", -100, 4000, 1, 1, 0, "200"),
    NUM("sats", D_ROUTER, "GPS", FL_ADV, "Satelliten", "", 0, 30, 1, 0, 0, "9"),
    ENUM("door", D_ROUTER, "Tür", 0, "Tür", "close", "close=Zu,open=Offen"),
    BOOL("mob_on", D_ROUTER, "Mobilfunk", FL_SET, "Mobilfunk senden", "ON"),
    NUM("rsrp", D_ROUTER, "Mobilfunk", 0, "RSRP", "dBm", -140, -44, 1, 0, 3, "-92"),
    NUM("rssi", D_ROUTER, "Mobilfunk", FL_ADV, "RSSI", "dBm", -110, -40, 1, 0, 2, "-65"),
    ENUM("net", D_ROUTER, "Mobilfunk", 0, "Netz", "LTE", "5G,LTE,3G,2G"),

    # ---- Zigbee hub (SMLIGHT cluster topics) ----
    TXT("zb_t", D_ZIGBEE, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic (data)", "camper/zigbee/data"),
    *zb_contact(1, "ON", "00158d0000000001", "Tür", "OFF"),
    *zb_contact(2, "ON", "00158d0000000002", "Fenster Küche", "OFF"),
    *zb_contact(3, "OFF", "00158d0000000003", "Dachhaube", "OFF"),
    *zb_climate(1, "ON", "00158d0000000011", "Kühlschrank", "5.5", "60"),
    *zb_climate(2, "OFF", "00158d0000000012", "Außen", "8.0", "75"),
    BOOL("l1_on", D_ZIGBEE, "LED-Leiste", 0, "Lampe vorhanden", "ON"),
    TXT("l1_ieee", D_ZIGBEE, "LED-Leiste", FL_SET | FL_TOPIC, "IEEE-Adresse", "a4c1380000000021"),
    BOOL("l1_state", D_ZIGBEE, "LED-Leiste", 0, "An", "OFF"),
    NUM("l1_level", D_ZIGBEE, "LED-Leiste", 0, "Helligkeit", "", 1, 254, 1, 0, 0, "180"),
    TXT("l1_color", D_ZIGBEE, "LED-Leiste", FL_ADV, "Farbe (letzter Befehl)", "#ffb060"),
]
FIDX = {f["key"]: i for i, f in enumerate(FIELDS)}

# ============================================================
# Scenarios
# ============================================================
# Same format for built-in ones, uploads and the download (README "Eigene Szenarien"):
#
#   {"id": "empty", "name": "Tanks leer", "desc": "...",
#    "start":   {"<field>": value, ...},                 set once when the scenario starts
#    "tracks":  {"<field>": [[t, value], ...], ...},     values over time (t in seconds)
#    "modes":   {"<device id>|$dev": [[t, "on|offline|off"], ...]},
#    "publish": [[t, "<topic>|@<field>", "payload"], ...],   one-shot messages
#    "on_stop": [["<topic>|@<field>", "payload"], ...],
#    "end": seconds, "keep": true}                       optional: stop by itself
#
# Tracks: numbers are interpolated linearly between the points, other values switch at a
# point. Before the first point the field is left alone, after the last one it stays. A
# track can also be {"points": [...], "loop": t0}: after the last point the part from t0
# on repeats. "$dev" is the device picked in the web page; "@<field>" takes the topic from
# that field. Topic fields (base topics, IEEE addresses) cannot be changed by a scenario.

BUILTIN_SCEN = [
    {"id": "empty", "name": "Tanks leer", "desc": "Frischwasser 30 → 5 %, Gas 25 → 8 % in 3 Minuten",
     "tracks": {"w_pct": [[0, 30], [180, 5]], "gas_pct": [[0, 25], [180, 8]]}},
    {"id": "fridge", "name": "Kühlschrank warm",
     "desc": "Kühlschrank (camper-temp und Zigbee-Klimasensor 1) 5 → 11 °C in 2 Minuten",
     "start": {"t1_on": "ON", "t1_ok": "ON"},
     "tracks": {"t1_t": [[0, 5], [120, 11]], "k1_t": [[0, 5.3], [120, 11.3]]}},
    {"id": "battery", "name": "Batterie leer", "desc": "Batterie 1 entlädt von 40 auf 12 % in 2½ Minuten",
     "start": {"b1_on": "ON", "b1_ok": "ON"},
     "tracks": {"b1_soc": [[0, 40], [140, 12]], "b1_v": [[0, 12.56], [140, 12.17]], "b1_a": [[0, -8.5]]}},
    {"id": "frost", "name": "Frost",
     "desc": "Außen (camper-temp Sensor 4 und Zigbee-Klimasensor 2) fällt von 4 auf −2 °C",
     "start": {"t4_on": "ON", "t4_ok": "ON", "k2_on": "ON"},
     "tracks": {"t4_t": [[0, 4], [90, -2]], "k2_t": [[0, 4.2], [90, -1.8]]}},
    {"id": "depart", "name": "Abfahrt",
     "desc": "Markise draußen, Tür und Kontakt 1 offen; Fahrt ab 20 s, Tür zu nach 60 s, Markise rein nach 90 s, "
             "Kontakt zu nach 120 s, Stillstand ab 150 s",
     "start": {"door": "open", "spd": 0, "fix": "ON"},
     "modes": {"router": [[0, "on"]]},
     "tracks": {"door": [[0, "open"], [60, "close"]], "c1_open": [[0, "ON"], [120, "OFF"]],
                "spd": [[0, 0], [20, 0], [45, 50], [150, 50], [150, 0]]},
     "publish": [[0, "@awn_t", "ON"], [90, "@awn_t", "OFF"]],
     "on_stop": [["@awn_t", "OFF"]]},
    {"id": "drive", "name": "Fahrt", "desc": "0 → 80 km/h, die Neigung schwankt, Kontakt 1 bleibt offen",
     "modes": {"router": [[0, "on"]]},
     "tracks": {"spd": [[0, 0], [10, 0], [63, 80]], "c1_open": [[0, "ON"]],
                "pitch": {"points": [[0, 0.3], [13, 0.3], [17.7, 1.8], [27.1, -1.2], [31.8, 0.3]], "loop": 13},
                "roll": {"points": [[0, -0.2], [13, -0.2], [19.3, 1.3], [31.9, -1.7], [44.4, 1.3]], "loop": 19.3}}},
    {"id": "tilt", "name": "Schräg geparkt", "desc": "3° nach vorne und 2,2° zur Seite, nach 90 s eben",
     "tracks": {"pitch": [[0, 3], [90, 0.2]], "roll": [[0, -2.2], [90, -0.3]]}},
    {"id": "heat", "name": "Aufheizen", "desc": "Heizung an auf 22 °C bei 12 °C Raumtemperatur, Warmwasser Eco",
     "start": {"alive": "ON", "hvac": "heat", "setp": 22, "room": 12, "wmode": "eco", "water": 15, "ht_dyn": "ON"}},
    {"id": "offline", "name": "Gerät offline",
     "desc": "das gewählte Gerät fällt nach 30 s aus (Last Will) und kommt nach 2 Minuten wieder",
     "modes": {"$dev": [[0, "on"], [30, "offline"], [150, "on"]]}},
]
BUILTIN_IDS = {s["id"] for s in BUILTIN_SCEN}

MAX_TRACKS, MAX_POINTS, MAX_EVENTS, MAX_T = 64, 500, 100, 86400


class ScenError(ValueError):
    pass


# ============================================================
# Value helpers
# ============================================================

def fmt(v, dec):
    """Number as published: fixed decimals, no "-0"."""
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return ""
    s = f"{v:.{dec}f}"
    if s.startswith("-") and float(s) == 0:
        s = s[1:]
    return s


def to_float(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return math.nan


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def jnum(x):
    """Number for a JSON list: integral values without ".0" (like ArduinoJson)."""
    return int(x) if float(x).is_integer() else x


def opt_values(f):
    return [p.split("=", 1)[0] for p in f["opts"].split(",")] if f["opts"] else []


def valid_topic(v, slash):
    if not v or len(v) > 60 or any(c in v for c in "#+ "):
        return False
    if not slash and "/" in v:
        return False
    return not (slash and (v[0] == "/" or v[-1] == "/"))


def normalize(i, v):
    """Checks and normalises a value for field i; raises ValueError with a German message."""
    f = FIELDS[i]
    v = str(v).strip()
    if f["type"] == "num":
        if not v:
            if f["flags"] & FL_OPT:
                return ""
            raise ValueError(f"{f['label']}: Zahl fehlt")
        try:
            x = float(v.replace(",", "."))
        except ValueError:
            raise ValueError(f"{f['label']}: keine Zahl") from None
        if math.isnan(x):
            raise ValueError(f"{f['label']}: keine Zahl")
        if x < f["min"] or x > f["max"]:
            raise ValueError(f"{f['label']}: erlaubt {fmt(f['min'], f['dec'])} bis {fmt(f['max'], f['dec'])}")
        return fmt(x, f["dec"])
    if f["type"] == "bool":
        u = v.upper()
        if u in ("ON", "1", "TRUE"):
            return "ON"
        if u in ("OFF", "0", "FALSE"):
            return "OFF"
        raise ValueError(f"{f['label']}: ON oder OFF")
    if f["type"] == "enum":
        if v in opt_values(f):
            return v
        raise ValueError(f"{f['label']}: unbekannte Auswahl")
    if len(v) > 80:
        raise ValueError(f"{f['label']}: zu lang")
    if f["flags"] & FL_TOPIC:
        full = "_pre" in f["key"] or f["key"].endswith("_t")
        if not valid_topic(v, full):
            raise ValueError(f"{f['label']}: ungültiges Topic")
    return v


def scalar_text(v):
    """Any JSON scalar as text: strings as they are, bools as ON / OFF, numbers as written."""
    if isinstance(v, bool):
        return "ON" if v else "OFF"
    if isinstance(v, str):
        return v
    if isinstance(v, (int, float)):
        return json.dumps(v)
    raise ScenError("Wert fehlt")


def scen_time(v):
    if isinstance(v, bool) or not isinstance(v, (int, float)) or math.isnan(v) or v < 0 or v > MAX_T:
        raise ScenError("Zeit muss eine Zahl in Sekunden sein (0 bis 86400)")
    return float(v)


MODE_NAMES = {"on": M_ON, "offline": M_OFFLINE, "pause": M_OFFLINE, "off": M_OFF}


def parse_track(src, field):
    """[[t, value], ...] or {"points": [...], "loop": t0}. field None = mode track."""
    loop = -1.0
    pts = src
    if isinstance(src, dict):
        pts = src.get("points")
        if src.get("loop") is not None:
            loop = scen_time(src["loop"])
    if not isinstance(pts, list) or not pts:
        raise ScenError("keine Punkte (erwartet [[Sekunde, Wert], …])")
    if len(pts) > MAX_POINTS:
        raise ScenError(f"höchstens {MAX_POINTS} Punkte je Verlauf")
    ts, xs, last = [], [], 0.0
    for p in pts:
        if not isinstance(p, list) or len(p) != 2:
            raise ScenError("Punkt muss [Sekunde, Wert] sein (Sekunde 0 bis 86400)")
        t, v = scen_time(p[0]), scalar_text(p[1])
        if t < last:
            raise ScenError("Zeiten müssen aufsteigend sein")
        last = t
        if field is None:
            if v not in MODE_NAMES:
                raise ScenError(f"Zustand „{v}“ unbekannt (on, offline, off)")
            xs.append(MODE_NAMES[v])
        else:
            try:
                v = normalize(field, v)
            except ValueError as e:
                raise ScenError(str(e)) from None
            xs.append(float(v) if FIELDS[field]["type"] == "num" else v)
        ts.append(t)
    if loop >= 0 and loop >= last:
        raise ScenError("„loop“ muss vor dem letzten Punkt liegen")
    return {"t": ts, "x": xs, "loop": loop}


def scen_field(key):
    i = FIDX.get(key)
    if i is None:
        raise ScenError(f"unbekanntes Feld „{key}“")
    if FIELDS[i]["flags"] & FL_TOPIC:
        raise ScenError(f"„{key}“ ist ein Topic und kann nicht geändert werden")
    return i


def scen_topic(v):
    if not isinstance(v, str):
        raise ScenError("Topic fehlt")
    if v.startswith("@"):
        i = FIDX.get(v[1:])
        if i is None or FIELDS[i]["type"] != "text":
            raise ScenError(f"„{v}“ ist kein Topic-Feld")
        return v
    if not valid_topic(v, True):
        raise ScenError(f"ungültiges Topic „{v}“")
    return v


def parse_scen(o):
    """Checks a scenario and returns its parsed form; raises ScenError with a German message."""
    if not isinstance(o, dict):
        raise ScenError("Jeder Eintrag muss ein Objekt sein")
    sid = o.get("id", "")
    if not isinstance(sid, str) or not (1 <= len(sid) <= 24) or not all(c in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in sid):
        raise ScenError("„id“ fehlt oder ist ungültig (1–24 Zeichen: a–z, 0–9, - und _)")
    pre = f"Szenario „{sid}“: "

    def sect(name, kind):
        v = o.get(name)
        if v is not None and not isinstance(v, kind):
            raise ScenError(pre + f"„{name}“ muss ein {'Objekt' if kind is dict else 'Liste'} sein")
        return v or kind()

    try:
        name = str(o.get("name") or sid).strip()
        desc = str(o.get("desc") or "")
        if not name or len(name) > 60:
            raise ScenError("„name“ 1–60 Zeichen")
        if len(desc) > 300:
            raise ScenError("„desc“ höchstens 300 Zeichen")
        end = 0.0
        if o.get("end") is not None:
            end = scen_time(o["end"])
            if end <= 0:
                raise ScenError("„end“ muss eine Zeit in Sekunden sein")
    except ScenError as e:
        raise ScenError(pre + str(e)) from None

    r = {"id": sid, "name": name, "desc": desc, "end": end, "keep": bool(o.get("keep", False)),
         "dev": False, "start": [], "tracks": [], "events": [], "on_stop": []}
    for k, v in sect("start", dict).items():
        try:
            i = scen_field(k)
            r["start"].append((i, normalize(i, scalar_text(v))))
        except (ScenError, ValueError) as e:
            raise ScenError(pre + "start: " + str(e)) from None
    for k, v in sect("tracks", dict).items():
        try:
            i = scen_field(k)
            r["tracks"].append(dict(parse_track(v, i), field=i, dev=0))
        except ScenError as e:
            raise ScenError(pre + f"tracks „{k}“: " + str(e)) from None
    for k, v in sect("modes", dict).items():
        try:
            if k == "$dev":
                d = -1
                r["dev"] = True
            else:
                d = next((n for n, x in enumerate(DEVS) if x[0] == k), None)
                if d is None:
                    raise ScenError("unbekanntes Gerät (" + ", ".join(x[0] for x in DEVS) + " oder $dev)")
            r["tracks"].append(dict(parse_track(v, None), field=-1, dev=d))
        except ScenError as e:
            raise ScenError(pre + f"modes „{k}“: " + str(e)) from None
    if len(r["tracks"]) > MAX_TRACKS:
        raise ScenError(pre + f"höchstens {MAX_TRACKS} Verläufe")
    for p in sect("publish", list):
        try:
            if not isinstance(p, list) or len(p) != 3:
                raise ScenError("Eintrag muss [Sekunde, Topic, Payload] sein")
            r["events"].append({"t": scen_time(p[0]), "topic": scen_topic(p[1]), "payload": scalar_text(p[2]), "done": False})
        except ScenError as e:
            raise ScenError(pre + "publish: " + str(e)) from None
    for p in sect("on_stop", list):
        try:
            if not isinstance(p, list) or len(p) != 2:
                raise ScenError("Eintrag muss [Topic, Payload] sein")
            r["on_stop"].append({"topic": scen_topic(p[0]), "payload": scalar_text(p[1])})
        except ScenError as e:
            raise ScenError(pre + "on_stop: " + str(e)) from None
    if len(r["events"]) + len(r["on_stop"]) > MAX_EVENTS:
        raise ScenError(pre + f"höchstens {MAX_EVENTS} Nachrichten")
    return r


def zb_val(v, quote):
    """SMLIGHT hub format: {"data":{"val":...,"type_sm":"report"}}"""
    return '{"data":{"val":' + (f'"{v}"' if quote else v) + ',"type_sm":"report"}}'


def compact(o):
    return json.dumps(o, ensure_ascii=False, separators=(",", ":"))


# ============================================================
# Emulator state
# ============================================================

class Emu:
    def __init__(self, data_dir: Path):
        self.lock = threading.RLock()
        self.data_dir = data_dir
        self.state_file = data_dir / "emu_state.json"
        self.scen_dir = data_dir / "scenarios"
        self.vals = [f["defv"] for f in FIELDS]
        self.noise_off = [0.0] * len(FIELDS)
        self.modes = [M_ON] * D_COUNT
        self.noise_on = [True] * D_COUNT
        self.topic_changed = [False] * D_COUNT
        self.clear_req = False
        self.run = None
        self.scen_dev = D_TANK
        self.scen_start = 0.0
        self.snapshot = None
        self.snap_modes = None
        self.save_at = 0.0
        self.last_noise = 0.0
        self.heki_last = math.nan
        self.sender = None                 # fn(topic, payload): not retained one-shot messages
        self.log = deque(maxlen=16)        # received commands
        self.scen_list = []
        self._load()
        self.topic_changed = [False] * D_COUNT   # nothing was sent yet
        self.scen_rebuild()

    # ---- field access ----
    def V(self, k):
        return self.vals[FIDX[k]]

    def N(self, k):
        return to_float(self.vals[FIDX[k]]) if self.vals[FIDX[k]] != "" else math.nan

    def NN(self, k):
        """Number with the current noise offset (only while noise is on for the device)."""
        i = FIDX[k]
        if self.vals[i] == "":
            return math.nan
        v = to_float(self.vals[i])
        if self.noise_on[FIELDS[i]["dev"]]:
            v += self.noise_off[i]
        return clamp(v, FIELDS[i]["min"], FIELDS[i]["max"])

    def B(self, k):
        return self.vals[FIDX[k]] == "ON"

    def P(self, k):
        return fmt(self.NN(k), FIELDS[FIDX[k]]["dec"])

    def set_raw(self, i, v, persist):
        if self.vals[i] == v:
            return
        if FIELDS[i]["flags"] & FL_TOPIC:
            self.topic_changed[FIELDS[i]["dev"]] = True
        self.vals[i] = v
        if persist and not self.run:
            self.mark_save()

    def set_num(self, k, x, persist):
        i = FIDX[k]
        f = FIELDS[i]
        if math.isnan(x):
            return
        self.set_raw(i, fmt(clamp(x, f["min"], f["max"]), f["dec"]), persist)

    def set_str(self, k, v, persist):
        self.set_raw(FIDX[k], v, persist)

    def set_bool(self, k, on, persist):
        self.set_str(k, "ON" if on else "OFF", persist)

    def prefix(self, d):
        return self.V(DEVS[d][2]) if DEVS[d][2] else ""

    def zigbee_cmd_base(self):
        b = self.V("zb_t")
        if b.endswith("/data"):
            b = b[:-5]
        return b + "/cmd/"

    # ---- persistence ----
    def mark_save(self):
        self.save_at = time.monotonic() + 2

    def _defaults(self, dev):
        for i, f in enumerate(FIELDS):
            if dev < 0 or f["dev"] == dev:
                if f["flags"] & FL_TOPIC and self.vals[i] != f["defv"]:
                    self.topic_changed[f["dev"]] = True
                self.vals[i] = f["defv"]

    def _save(self):
        doc = {"modes": [M_ON if m == M_OFFLINE else m for m in self.modes], "noise": self.noise_on,
               "values": {f["key"]: self.vals[i] for i, f in enumerate(FIELDS) if self.vals[i] != f["defv"]}}
        self.data_dir.mkdir(parents=True, exist_ok=True)
        tmp = self.state_file.with_suffix(".tmp")
        tmp.write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
        tmp.replace(self.state_file)

    def _load(self):
        try:
            doc = json.loads(self.state_file.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return
        except (OSError, ValueError) as e:
            print(f"[EMU] {self.state_file} unreadable ({e}), using the examples")
            return
        for d, m in enumerate(doc.get("modes", [])[:D_COUNT]):
            self.modes[d] = M_OFF if m == M_OFF else M_ON
        for d, n in enumerate(doc.get("noise", [])[:D_COUNT]):
            self.noise_on[d] = bool(n)
        for k, v in (doc.get("values") or {}).items():
            if k in FIDX:
                try:
                    self.vals[FIDX[k]] = normalize(FIDX[k], v)   # a stale or edited file must not break anything
                except ValueError:
                    pass

    # ---- scenario storage ----
    def _custom_files(self):
        return sorted(self.scen_dir.glob("*.json")) if self.scen_dir.is_dir() else []

    def scen_rebuild(self):
        lst = []
        for s in BUILTIN_SCEN:
            r = parse_scen(s)   # built-in ones must always be valid
            lst.append(dict(r, builtin=True, src=s))
        for p in self._custom_files():
            try:
                src = json.loads(p.read_text(encoding="utf-8"))
                r = parse_scen(src)
                if r["id"] != p.stem or r["id"] in BUILTIN_IDS:
                    raise ScenError("id passt nicht zum Dateinamen")
            except (OSError, ValueError) as e:
                print(f"[EMU] Skipping scenario file {p.name}: {e}")
                continue
            lst.append(dict(r, builtin=False, src=src))
        self.scen_list = lst

    def scen_by_id(self, sid):
        return next((s for s in self.scen_list if s["id"] == sid), None)

    # ---- loop (once a second) ----
    def tick(self):
        with self.lock:
            now = time.monotonic()
            if now - self.last_noise >= 10 or self.last_noise == 0:
                self.last_noise = now
                self.noise_off = [random.uniform(-f["noise"], f["noise"]) if f["noise"] > 0 else 0.0 for f in FIELDS]
            if self.run:
                self._scenario_step(now - self.scen_start)
            self._heater_dynamics()
            self._heki_dynamics()
            if self.save_at and now >= self.save_at:
                self.save_at = 0
                self._save()

    def flush(self):
        with self.lock:
            if self.save_at:
                self.save_at = 0
                self._save()

    def _send_event(self, ev):
        topic = ev["topic"]
        if topic.startswith("@"):
            topic = self.V(topic[1:])
        if self.sender and topic:
            self.sender(topic, ev["payload"])

    @staticmethod
    def _track_at(k, t):
        ts = k["t"]
        last = ts[-1]
        if k["loop"] >= 0 and t > last:
            t = k["loop"] + math.fmod(t - k["loop"], last - k["loop"])
        if t < ts[0]:
            return None
        i = 0
        while i + 1 < len(ts) and ts[i + 1] <= t:
            i += 1
        frac = (t - ts[i]) / (ts[i + 1] - ts[i]) if i + 1 < len(ts) else 0
        return i, frac

    def _scenario_step(self, t):
        r = self.run
        for k in r["tracks"]:
            at = self._track_at(k, t)
            if at is None:
                continue
            i, f = at
            if k["field"] < 0:
                self.modes[self.scen_dev if k["dev"] < 0 else k["dev"]] = k["x"][i]
            elif FIELDS[k["field"]]["type"] == "num":
                x = k["x"][i] + ((k["x"][i + 1] - k["x"][i]) * f if f > 0 else 0)
                fd = FIELDS[k["field"]]
                self.set_raw(k["field"], fmt(clamp(x, fd["min"], fd["max"]), fd["dec"]), False)
            else:
                self.set_raw(k["field"], k["x"][i], False)
        for ev in r["events"]:
            if not ev["done"] and t >= ev["t"]:
                ev["done"] = True
                self._send_event(ev)
        if r["end"] > 0 and t >= r["end"]:
            self.stop_scenario(not r["keep"])

    def _heater_dynamics(self):
        if not self.B("ht_dyn") or not self.B("alive"):
            return
        room, setp, water = self.N("room"), self.N("setp"), self.N("water")
        heat = self.V("hvac") == "heat"
        target = setp if heat else 12.0            # without heating it cools to 12 °C
        rate = (0.03 if self.V("fan") == "eco" else 0.06) if heat else 0.01
        if not math.isnan(room) and abs(room - target) > 0.05:
            self.set_num("room", room + (rate if target > room else -rate), False)
        wm = self.V("wmode")
        wt = {"eco": 40, "high": 60, "boost": 62}.get(wm, 15)
        if not math.isnan(water) and abs(water - wt) > 0.5:
            self.set_num("water", water + (0.25 if wt > water else -0.05), False)

    def _heki_dynamics(self):
        """Roof hatch moves towards the target like the real one; a typed position becomes the target."""
        if not self.B("rv_on"):
            self.heki_last = math.nan
            return
        pos, tgt = self.N("rv_pos"), self.N("rv_tgt")
        if math.isnan(pos) or math.isnan(tgt):
            return
        if not math.isnan(self.heki_last) and pos != self.heki_last:
            self.set_num("rv_tgt", pos, True)
            self.heki_last = pos
            return
        if self.B("rv_conn") and pos != tgt:
            st = max(1.0, self.N("rv_speed"))
            pos = min(tgt, pos + st) if pos < tgt else max(tgt, pos - st)
            self.set_num("rv_pos", pos, pos == tgt)   # save only the end position
        self.heki_last = self.N("rv_pos")

    # ============================================================
    # Payload builders: same topics and formats as the real firmware
    # ============================================================

    def build(self, d):
        """All retained (topic, payload) pairs of device d."""
        with self.lock:
            out = []
            emit = lambda t, p: out.append((t, p))   # noqa: E731
            p = self.prefix(d)
            [self._b_tank, self._b_level, self._b_heater, self._b_fan, self._b_temp, self._b_battery,
             lambda p, e: self._b_router(e), lambda p, e: self._b_zigbee(e), self._b_toolbox][d](p, emit)
            return out

    def _b_tank(self, p, emit):
        for sub, pct, cap, filt in (("water", "w_pct", "w_cap", "w_filter"), ("grey", "g_pct", "g_cap", "g_filter")):
            if sub == "grey" and not self.B("g_on"):
                continue
            v = self.NN(pct)
            b = f"{p}/{sub}/"
            emit(b + "percent", fmt(v, 0))
            emit(b + "liters", fmt(v * self.N(cap) / 100, 1))
            emit(b + "voltage", fmt(0.2 + v / 100 * 1.8, 2))   # sender voltage, rough
            emit(b + "filter_active", self.V(filt))
        if self.V("gas_state") == "OK":   # values only while the BLE sensor answers, like the firmware
            g = self.NN("gas_pct")
            emit(p + "/gas/level", fmt(g, 0))
            emit(p + "/gas/raw_level", fmt(g, 1))
            emit(p + "/gas/content", fmt(g * self.N("gas_kg") / 100, 2))
            emit(p + "/gas/battery", self.P("gas_bat"))
            emit(p + "/gas/ble_rssi", self.P("gas_rssi"))
        emit(p + "/gas/ble_state", self.V("gas_state"))

    def _b_level(self, p, emit):
        st = self.V("lv_sensor")
        emit(p + "/sensor", st)
        emit(p + "/tolerance", self.P("tol"))
        emit(p + "/tare_active", self.V("lv_tare"))
        if st != "OK":
            return
        pi, ro, tol = self.NN("pitch"), self.NN("roll"), self.N("tol")
        emit(p + "/pitch", fmt(pi, 1))
        emit(p + "/roll", fmt(ro, 1))
        emit(p + "/is_level", "ON" if abs(pi) <= tol and abs(ro) <= tol else "OFF")
        # wedge under each wheel in cm: lift every wheel to the highest one
        # (pitch > 0 = front higher, roll > 0 = left higher)
        tp, tr = math.tan(math.radians(pi)), math.tan(math.radians(ro))
        hb, ht = self.N("wb") / 2, self.N("tw") / 2
        h = [hb * tp + ht * tr, hb * tp - ht * tr, -hb * tp + ht * tr, -hb * tp - ht * tr]
        top = max(h)
        for name, x in zip(("fl", "fr", "rl", "rr"), h):
            emit(f"{p}/wedge_{name}", fmt((top - x) * 100, 1))

    def _b_heater(self, p, emit):
        cs = p + "/control_status/"
        alive = self.B("alive")
        emit(cs + "alive", "ON" if alive else "OFF")
        heat = self.V("hvac") == "heat"
        room, setp, water = self.NN("room"), self.N("setp"), self.N("water")
        wm, fan = self.V("wmode"), self.V("fan")
        heating = heat and room < setp - 0.2
        water_heating = wm != "off" and water < (40 if wm == "eco" else 60) - 1
        emit(cs + "current_temp_room", fmt(room, 1))
        emit(cs + "room_setpoint", fmt(setp, 1))
        emit(cs + "target_temp_room", fmt(setp, 0) if heat else "0")
        emit(cs + "hvac_mode", "heat" if heat else "off")
        emit(cs + "hvac_action", "off" if not heat and wm == "off" else "heating" if heating or water_heating else "idle")
        emit(cs + "fan_mode", fan)
        emit(cs + "heating_mode", ("eco" if fan == "eco" else "high") if heat else "off")
        emit(cs + "water_mode", "high" if wm == "boost" else wm)
        emit(cs + "target_temp_water", {"eco": "40", "high": "60", "boost": "200"}.get(wm, "0"))
        emit(cs + "current_temp_water", fmt(water, 1))
        on = alive and (heat or wm != "off")
        emit(cs + "operating_status", "5" if on else "0")
        emit(cs + "operating_status_text", "Keine Verbindung" if not alive else "Aus" if not on
             else "Heizen" if heating or water_heating else "Bereit")
        err = int(self.N("ht_err"))
        emit(cs + "error_code", str(err))
        emit(cs + "heater_error", "ON" if err else "OFF")
        emit(cs + "decoke", self.V("decoke"))
        emit(cs + "decoke_remaining", self.P("decoke_rem"))
        emit(cs + "energy_mix", "gas")
        emit(cs + "heater_model", "combi_d")

    def _b_fan(self, p, emit):
        emit(p + "/expander", self.V("mf_exp"))
        for k in ("p1", "p2"):
            if not self.B(k + "_on"):
                continue
            emit(f"{p}/{k}/auto_hold", self.V(k + "_hold"))
            emit(f"{p}/{k}/led", self.V(k + "_led"))

    def _b_toolbox(self, p, emit):
        """Same topics as plugin_rvtech of camper-toolbox: <prefix>/rvtech/..."""
        if not self.B("rv_on"):
            return
        r = p + "/rvtech/"
        conn = self.B("rv_conn")
        emit(r + "state", "Verbunden" if conn else "Nicht erreichbar")
        emit(r + "connected", "ON" if conn else "OFF")
        emit(r + "release", "OFF")
        pos, tgt = round(self.N("rv_pos")), round(self.N("rv_tgt"))
        emit(r + "cover", "opening" if pos < tgt else "closing" if pos > tgt else "closed" if pos == 0 else "open")
        emit(r + "cover_position", str(min(pos, 100)))
        emit(r + "reported", str(pos))

    def _b_temp(self, p, emit):
        lst, text = [], []
        for n in range(1, 5):
            k = f"t{n}_"
            if not self.B(k + "on"):
                continue
            sid, name, ok = self.V(k + "id"), self.V(k + "name"), self.B(k + "ok")
            t, lo, hi = self.NN(k + "t"), self.N(k + "lo"), self.N(k + "hi")
            alarm = 0 if not ok else 2 if (not math.isnan(hi) and t > hi) else 1 if (not math.isnan(lo) and t < lo) else 0
            if ok:
                emit(f"{p}/{sid}", fmt(t, 2))
            emit(f"{p}/{sid}/available", "online" if ok else "offline")
            emit(f"{p}/{sid}/alarm", "ON" if alarm else "OFF")
            if alarm:
                c = lambda x: fmt(x, 1).replace(".", ",")   # noqa: E731
                text.append(f"{name} {'zu warm' if alarm == 2 else 'zu kalt'}: {c(t)} °C (Grenze {c(hi if alarm == 2 else lo)} °C)")
            lst.append({"id": sid, "name": name, "t": jnum(round(t, 2)) if ok else None, "alarm": alarm})
        emit(p + "/alarm", "ON" if text else "OFF")
        emit(p + "/alarm_text", "; ".join(text))
        emit(p + "/sensors", compact(lst))

    def _b_battery(self, p, emit):
        lst = []
        for n in (1, 2):
            k = f"b{n}_"
            if not self.B(k + "on"):
                continue
            bid = self.V(k + "id")
            b = f"{p}/{bid}/"
            btype = int(self.V(k + "type"))
            ok, cur = self.B(k + "ok"), btype != 3     # BM2 has no current sensor
            soc, v = self.NN(k + "soc"), self.NN(k + "v")
            a = self.NN(k + "a") if cur else math.nan
            ah = soc / 100 * self.N(k + "cap")
            prob = self.B(k + "prob")
            pt = self.V(k + "pt") if prob else ""
            st = ("Kritisch" if soc < 20 else "Schwach" if soc < 50 else "Normal") if btype == 3 else None
            rt = ah / -a * 60 if cur and a < -0.05 else math.nan   # minutes while discharging
            emit(b + "available", "online" if ok else "offline")
            o = {"id": bid, "name": self.V(k + "name"), "ok": ok, "t": btype}
            lst.append(o)
            if not ok:
                continue
            emit(b + "voltage", fmt(v, 2))
            emit(b + "soc", fmt(soc, 0))
            if cur:
                emit(b + "current", fmt(a, 2))
                emit(b + "power", fmt(v * a, 0))
                emit(b + "charge", fmt(ah, 1))
                emit(b + "energy", fmt(ah * v, 0))
                emit(b + "runtime", "None" if math.isnan(rt) else fmt(rt, 0))
            if st:
                emit(b + "state", st)
            if btype == 1:
                emit(b + "temperature", self.P(k + "temp"))
                emit(b + "cycles", self.P(k + "cyc"))
            emit(b + "charging", "ON" if cur and a > 0.05 else "OFF")
            emit(b + "problem", "ON" if prob else "OFF")
            emit(b + "problem_text", pt)
            o["soc"] = jnum(round(soc))
            o["v"] = jnum(round(v, 2))
            if cur:
                o["a"] = jnum(round(a, 2))
                o["w"] = jnum(round(v * a))
            if st:
                o["st"] = st
            if not math.isnan(rt):
                o["rt"] = round(rt)
            if prob:
                o["p"] = True
                o["pt"] = pt
        emit(p + "/batteries", compact(lst))

    def _b_router(self, emit):
        # same shape as the RUT956 "Data to Server" JSON: values as text, speed in km/h
        now = time.time()
        spd = self.N("spd")
        if spd > 1 and self.noise_on[D_ROUTER]:
            spd += self.noise_off[FIDX["rsrp"]] / 3   # a little movement while driving
        fix = self.B("fix")
        doc = {"TIME": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now)),
               "GPS": {"latitude": self.V("lat"), "longitude": self.V("lon"), "altitude": self.V("alt"),
                       "speed": fmt(max(0.0, spd), 1), "accuracy": "0.9" if fix else "0",
                       "satellites": self.V("sats") if fix else "0", "fix_status": "1" if fix else "0",
                       "timestamp": str(int(now))}}
        emit(self.V("rut_t"), compact(doc))
        emit(self.V("door_t"), compact({"Door": {"name": "din2", "type": "Digital In", "state": self.V("door"),
                                                 "custom_name": "Door"}}))
        if self.B("mob_on"):
            emit(self.V("mob_t"), compact({"rsrp": round(self.NN("rsrp")), "rssi": round(self.NN("rssi")),
                                           "conntype": self.V("net")}))

    def _b_zigbee(self, emit):
        base = self.V("zb_t")
        for n in (1, 2, 3):
            k = f"c{n}_"
            if self.B(k + "on"):
                b = f"{base}/{self.V(k + 'ieee')}/1/"
                emit(b + "0006/0000", zb_val("1" if self.B(k + "open") else "0", False))
                emit(b + "0001/0021", zb_val(self.P(k + "bat"), False))
        for n in (1, 2):
            k = f"k{n}_"
            if self.B(k + "on"):
                b = f"{base}/{self.V(k + 'ieee')}/1/"
                emit(b + "0402/0000", zb_val(self.P(k + "t"), False))
                emit(b + "0405/0000", zb_val(self.P(k + "h"), False))
                emit(b + "0001/0021", zb_val(self.P(k + "bat"), False))
        if self.B("l1_on"):
            b = f"{base}/{self.V('l1_ieee')}/1/"
            emit(b + "0006/0000", zb_val(self.V("l1_state"), True))
            emit(b + "0008/0000", zb_val(self.V("l1_level"), False))

    # ============================================================
    # Commands: react like the real device would
    # ============================================================

    @staticmethod
    def _on_off(msg, current):
        u = msg.strip().upper()
        if u in ("ON", "1", "TRUE"):
            return True
        if u in ("OFF", "0", "FALSE"):
            return False
        if u == "TOGGLE":
            return not current
        return None

    def command(self, topic, msg):
        with self.lock:
            self.log.appendleft((time.monotonic(), topic, msg if len(msg) <= 60 else msg[:60] + "…"))
            zb = self.zigbee_cmd_base()
            if topic.startswith(zb):
                self._c_zigbee(topic[len(zb):], msg)
                return
            for d, dev in enumerate(DEVS):
                if not dev[3]:
                    continue
                p = self.prefix(d) + "/"
                if not topic.startswith(p):
                    continue
                sub = topic[len(p):]
                fn = {D_TANK: self._c_tank, D_LEVEL: self._c_level, D_HEATER: self._c_heater,
                      D_FAN: self._c_fan, D_TOOLBOX: self._c_toolbox}.get(d)
                if fn:
                    fn(sub, msg)
                return

    def _c_tank(self, sub, msg):
        for t, k in (("cmd/water/filter/set", "w_filter"), ("cmd/grey/filter/set", "g_filter")):
            if sub == t:
                on = self._on_off(msg, self.B(k))
                if on is not None:
                    self.set_bool(k, on, True)

    def _c_level(self, sub, msg):
        if sub == "cmd/tare/set":   # the real sensor stores the current angles as zero point
            self.set_num("pitch", 0, True)
            self.set_num("roll", 0, True)
            self.set_bool("lv_tare", True, True)
        elif sub == "cmd/tare_reset/set":
            self.set_bool("lv_tare", False, True)
        elif sub == "cmd/tolerance/set":
            self.set_num("tol", to_float(msg), True)
        elif sub == "cmd/sensor_restart/set":
            self.set_str("lv_sensor", "OK", True)

    def _c_heater(self, sub, msg):
        if not sub.startswith("set/"):
            return
        key, v = sub[4:], msg.strip().lower()
        x = to_float(v)
        x0 = 0.0 if math.isnan(x) else x
        if key == "target_temp_room":
            if x0 <= 0:
                self.set_str("hvac", "off", True)
            else:
                self.set_num("setp", x0, True)
                self.set_str("hvac", "heat", True)
        elif key == "room_setpoint":
            if 5 <= x0 <= 30:
                self.set_num("setp", x0, True)
        elif key == "hvac_mode":
            if v in ("heat", "off"):
                self.set_str("hvac", v, True)
        elif key == "heating_mode":
            if v == "off":
                self.set_str("hvac", "off", True)
            elif v in ("eco", "high"):
                self.set_str("fan", v, True)
                self.set_str("hvac", "heat", True)
        elif key == "fan_mode":
            if v in ("eco", "high", "boost"):
                self.set_str("fan", v, True)
        elif key == "water_mode":
            if v in ("off", "eco", "high", "boost"):
                self.set_str("wmode", v, True)
        elif key == "target_temp_water":
            self.set_str("wmode", "boost" if x0 >= 200 else "high" if x0 >= 60 else "eco" if x0 >= 40 else "off", True)
        elif key == "decoke":
            on = self._on_off(msg, self.B("decoke"))
            if on is not None:
                self.set_bool("decoke", on, True)
                self.set_num("decoke_rem", 60 if on else 0, True)

    def _c_fan(self, sub, msg):
        # cmd/p<n>/...
        if not sub.startswith("cmd/p") or len(sub) < 7:
            return
        k, rest = sub[4:6], sub[7:]
        if k not in ("p1", "p2"):
            return
        sp, led, hold = k + "_speed", k + "_led", k + "_hold"
        s = self.N(sp)
        btn = (msg if rest == "press" else rest).strip()
        if rest == "speed/set":
            self.set_num(sp, to_float(msg), True)
        elif rest == "close":
            self.set_num(sp, 0, True)
        elif rest == "led/set":
            on = self._on_off(msg, self.B(led))
            if on is not None:
                self.set_bool(led, on, True)
        elif btn == "faster":
            self.set_num(sp, s + 1, True)
        elif btn == "slower":
            self.set_num(sp, s - 1, True)
        elif btn == "on_off":
            self.set_num(sp, 0 if s > 0 else 3, True)
        elif btn == "auto":
            self.set_bool(hold, not self.B(hold), True)

    def _c_toolbox(self, sub, msg):
        # cmd/rvtech/<key>/set, keys and preset values as in plugin_rvtech
        if not sub.startswith("cmd/rvtech/") or not sub.endswith("/set") or not self.B("rv_on"):
            return
        key, v = sub[11:-4], msg.strip()
        t = -1
        if key == "cover":
            t = 100 if v.upper() == "OPEN" else 0 if v.upper() == "CLOSE" else -1
        elif key == "cover_position":
            try:
                t = int(v)
            except ValueError:
                t = -1
            if not 0 <= t <= 100:
                t = -1
        elif key in ("auf", "zu", "regen", "lueften"):
            t = {"auf": 100, "zu": 0, "regen": 12, "lueften": 102}[key]
        elif key == "release":
            self.set_bool("rv_conn", not (v in ("ON", "1")), True)
            return
        if t >= 0:
            self.set_num("rv_tgt", t, True)

    def _c_zigbee(self, rest, msg):
        # <ieee>/1/<cluster>[/<attribute>]
        ieee, _, path = rest.partition("/")
        if not ieee or not self.B("l1_on") or ieee.lower() != self.V("l1_ieee").lower():
            return
        if path == "1/0006":
            on = self._on_off(msg, self.B("l1_state"))
            if on is not None:
                self.set_bool("l1_state", on, True)
        elif path == "1/0008/0000":
            self.set_num("l1_level", to_float(msg), True)
            self.set_bool("l1_state", True, True)
        elif path in ("1/0300/0007", "1/0300/000a"):
            self.set_str("l1_color", msg[:80], True)
            self.set_bool("l1_state", True, True)

    # ============================================================
    # Web page
    # ============================================================

    def schema(self):
        with self.lock:
            fields = []
            for f in FIELDS:
                o = {"k": f["key"], "d": f["dev"], "t": f["type"], "l": f["label"], "def": f["defv"]}
                if f["group"]:
                    o["g"] = f["group"]
                if f["unit"]:
                    o["u"] = f["unit"]
                if f["type"] == "num":
                    o.update(min=f["min"], max=f["max"], step=f["step"])
                if f["opts"]:
                    o["o"] = f["opts"]
                if f["flags"] & FL_SET:
                    o["set"] = True
                if f["flags"] & FL_ADV:
                    o["adv"] = True
                if f["flags"] & FL_OPT:
                    o["opt"] = True
                fields.append(o)
            scen = []
            for s in self.scen_list:
                o = {"id": s["id"], "name": s["name"], "desc": s["desc"]}
                if not s["builtin"]:
                    o["custom"] = True
                if s["dev"]:
                    o["dev"] = True
                if s["end"] > 0:
                    o["end"] = jnum(s["end"])
                scen.append(o)
            return {"devices": [{"id": d[0], "name": d[1], "own": d[3]} for d in DEVS], "fields": fields, "scenarios": scen}

    def state(self):
        with self.lock:
            now = time.monotonic()
            o = {"modes": list(self.modes), "noise": list(self.noise_on),
                 "v": {f["key"]: self.vals[i] for i, f in enumerate(FIELDS)},
                 "log": [{"age": int(now - ts), "t": t, "p": p} for ts, t, p in self.log]}
            if self.run:
                s = {"id": self.run["id"], "name": self.run["name"], "t": int(now - self.scen_start)}
                if self.run["dev"]:
                    s["dev"] = self.scen_dev
                if self.run["end"] > 0:
                    s["end"] = jnum(self.run["end"])
                o["scenario"] = s
            return o

    def set_value(self, key, value):
        with self.lock:
            i = FIDX.get(key)
            if i is None:
                raise ValueError(f"Unbekanntes Feld {key}")
            v = normalize(i, "" if value is None else value)
            self.set_raw(i, v, True)
            if self.run and self.snapshot:
                self.snapshot[i] = v   # a manual change survives "stop"

    def set_mode(self, d, m):
        if not (0 <= d < D_COUNT) or m not in (M_OFF, M_ON, M_OFFLINE):
            raise ValueError("Ungültiger Zustand")
        with self.lock:
            self.modes[d] = m
            self.mark_save()

    def set_noise(self, d, on):
        if 0 <= d < D_COUNT:
            with self.lock:
                self.noise_on[d] = bool(on)
                self.mark_save()

    def start_scenario(self, sid, dev):
        with self.lock:
            s = self.scen_by_id(sid)
            if not s:
                raise ValueError("Unbekanntes Szenario")
            r = parse_scen(s["src"])   # fresh copy: events carry a "done" flag
            if self.run:
                self.stop_scenario(True)
            self.snapshot = list(self.vals)
            self.snap_modes = list(self.modes)
            self.scen_dev = dev if 0 <= dev < D_COUNT and DEVS[dev][3] else D_TANK   # only own connections can drop out
            self.run = r
            self.scen_start = time.monotonic()
            for i, v in r["start"]:
                self.set_raw(i, v, False)
            print(f"[EMU] Scenario {sid} started")

    def stop_scenario(self, restore):
        with self.lock:
            if not self.run:
                return
            for ev in self.run["on_stop"]:
                self._send_event(ev)
            if restore and self.snapshot:
                for i, v in enumerate(self.snapshot):
                    self.set_raw(i, v, False)
                self.modes = list(self.snap_modes)
            print(f"[EMU] Scenario {self.run['id']} stopped" + (", values restored" if restore else ""))
            self.run = None
            if not restore:
                self.mark_save()

    def import_scenarios(self, doc):
        """Upload: {"scenarios": [...]}, a list or one scenario. All or nothing; built-in ids are skipped."""
        if isinstance(doc, dict) and isinstance(doc.get("scenarios"), list):
            lst = doc["scenarios"]
        elif isinstance(doc, list):
            lst = doc
        elif isinstance(doc, dict) and isinstance(doc.get("id"), str):
            lst = [doc]
        else:
            lst = []
        if not lst:
            raise ValueError('Keine Szenarien gefunden (erwartet {"scenarios": [...]})')
        with self.lock:
            skipped, todo = [], {}
            for o in lst:
                r = parse_scen(o)
                if r["id"] in BUILTIN_IDS:
                    skipped.append(r["id"])
                    continue
                if r["id"] in todo:
                    raise ValueError(f"Szenario „{r['id']}“ ist doppelt in der Datei")
                todo[r["id"]] = {k: v for k, v in o.items() if k != "builtin"}
            added = sum(1 for sid in todo if not self.scen_by_id(sid))
            self.scen_dir.mkdir(parents=True, exist_ok=True)
            for sid, o in todo.items():
                if self.run and self.run["id"] == sid:
                    self.stop_scenario(True)
                (self.scen_dir / f"{sid}.json").write_text(json.dumps(o, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            self.scen_rebuild()
            report = f"{added} neu, {len(todo) - added} ersetzt"
            if skipped:
                report += f", {len(skipped)} Standard übersprungen ({', '.join(skipped)})"
            print(f"[EMU] Scenario upload: {report}")
            return report

    def delete_scenario(self, sid):
        with self.lock:
            s = self.scen_by_id(sid)
            if not s:
                raise ValueError("Unbekanntes Szenario")
            if s["builtin"]:
                raise ValueError("Standard-Szenarien lassen sich nicht löschen")
            if self.run and self.run["id"] == sid:
                self.stop_scenario(True)
            (self.scen_dir / f"{sid}.json").unlink(missing_ok=True)
            self.scen_rebuild()

    def export_scenarios(self):
        with self.lock:
            out = []
            for s in self.scen_list:
                o = dict(s["src"])
                if s["builtin"]:
                    o["builtin"] = True
                out.append(o)
            return {"format": "camper-emulator-scenarios", "version": 1, "scenarios": out}

    def reset_values(self, dev):
        with self.lock:
            if self.run:
                self.stop_scenario(False)
            self._defaults(dev)
            self.mark_save()

    def request_clear_all(self):
        with self.lock:
            self.clear_req = True

    def take_clear_all(self):
        with self.lock:
            r, self.clear_req = self.clear_req, False
            return r

    def take_topic_change(self, d):
        with self.lock:
            r, self.topic_changed[d] = self.topic_changed[d], False
            return r
