#!/usr/bin/env python3
"""Regenerate the LVGL fonts (Montserrat + umlauts + Font Awesome 5 icons).

Needs node/npx (lv_font_conv 1.5.3 is fetched by npx) and the LVGL library
(Montserrat-Medium.ttf and FontAwesome5-Solid+Brands+Regular.woff from lvgl/scripts/built_in_font).
    python3 tools/make_fonts.py [path/to/lvgl/scripts/built_in_font]
Standard library only.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, '..', '_arduino', 'camper_display'))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else
                      os.path.expanduser('~/Arduino/libraries/lvgl/scripts/built_in_font'))
TEXT = '0x20-0x7E,0xB0,0xB7,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0x2013,0x2022,0x2026'

# LVGL built-in symbols (LV_SYMBOL_*)
LV_SYMBOLS = [61441, 61448, 61451, 61452, 61453, 61457, 61459, 61461, 61465, 61468, 61473, 61478, 61479, 61480, 61502,
              61507, 61512, 61515, 61516, 61517, 61521, 61522, 61523, 61524, 61543, 61544, 61550, 61552, 61553, 61556,
              61559, 61560, 61561, 61563, 61587, 61589, 61636, 61637, 61639, 61641, 61664, 61671, 61674, 61683, 61724,
              61732, 61787, 61931, 62016, 62017, 62018, 62019, 62020, 62087, 62099, 62212, 62189, 62810, 63426, 63650]
# icons of the UI (fire, gas, balance, tint, wind, drip, thermometer, ...)
UI_ICONS = [61549, 62153, 62767, 62172, 62030, 63347, 61548, 63337, 63278, 62838, 62461, 62609, 61760, 61531, 61618]
# weather: sun, moon, cloud, cloud-sun, cloud-moon, cloud-rain, showers, sun-rain, snowflake, bolt, smog,
# wind, umbrella, thermometer-half, map-marker, location-arrow, tint
WEATHER = [0xF185, 0xF186, 0xF0C2, 0xF6C4, 0xF6C3, 0xF73D, 0xF740, 0xF743, 0xF2DC, 0xF0E7, 0xF75F,
           0xF72E, 0xF0E9, 0xF2C9, 0xF3C5, 0xF124, 0xF043]
ICONS = sorted(set(LV_SYMBOLS + UI_ICONS + WEATHER))

FONTS = {
    14: (TEXT, ICONS),
    18: (TEXT, ICONS),
    22: (TEXT, ICONS),
    28: (TEXT, ICONS),
    # large digits for temperatures plus the weather icons
    56: ('0x20,0x25,0x2B-0x3A,0x43,0x4C,0x56,0xB0', WEATHER),
}


def main():
    mont = os.path.join(SRC, 'Montserrat-Medium.ttf')
    fa = os.path.join(SRC, 'FontAwesome5-Solid+Brands+Regular.woff')
    for size, (text, icons) in FONTS.items():
        name = f'font_de_{size}'
        path = os.path.join(OUT, name + '.c')
        cmd = ['npx', '-y', 'lv_font_conv@1.5.3', '--no-compress', '--bpp', '4', '--size', str(size),
               '--font', mont, '-r', text, '--font', fa, '-r', ','.join(str(i) for i in icons),
               '--format', 'lvgl', '-o', path, '--force-fast-kern-format', '--lv-font-name', name]
        subprocess.run(cmd, check=True)
        # keep the files independent of local paths
        s = open(path).read().replace(SRC + '/', '').replace(OUT + '/', '')
        s = s.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')   # Arduino library layout
        open(path, 'w').write(s)
        print(name, len(icons), 'icons')


if __name__ == '__main__':
    main()
