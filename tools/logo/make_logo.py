#!/usr/bin/env python3
"""Build all VanNode logo files.

Symbol: hexagon with van, network and antenna. Wordmark: "vannode" in Poppins Bold
(converted to paths, so the SVGs need no font), the "o" is a node.

Output:
  docs/logo/vannode-logo.svg / .png          symbol + wordmark, for light backgrounds
  docs/logo/vannode-logo-dark.svg / .png     the same for dark backgrounds
  docs/logo/vannode-symbol.svg / .png        symbol only (icons, favicon)
  docs/logo/vannode-wordmark(-dark).svg      wordmark only
  firmware/roadbuddy/roadbuddy/logo_img.h    Road Buddy start page (symbol above wordmark, RGB565)
  firmware/display/camper_display/logo_img.c Wall panel: symbol in the header and in the QR code (LVGL images)
  firmware/_common/web/vannode.css           .vn = symbol left of the page title (all web pages)
  firmware/_common/web/vannode-icon.html     favicon (all web pages)
  After a change: python3 tools/common/sync_common.py, then pack/embed the web pages.

Run in a venv (cairosvg needs the cairo library of the system):
  python3 -m venv .venv && . .venv/bin/activate
  pip install -r tools/logo/requirements.txt
  python3 tools/logo/make_logo.py
"""
from io import BytesIO
from pathlib import Path
from urllib.parse import quote

import cairosvg
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "docs" / "logo"
RB_HEADER = ROOT / "firmware" / "roadbuddy" / "roadbuddy" / "logo_img.h"
PANEL_IMG = ROOT / "firmware" / "display" / "camper_display" / "logo_img.c"
COMMON_WEB = ROOT / "firmware" / "_common" / "web"
PANEL_HDR = 28                  # wall panel: symbol size in the 40 px header
PANEL_QR = 24                   # wall panel: symbol in the middle of the QR code (incl. white margin)

NAVY, BLUE, LIGHT = "#1E2A36", "#2F8AE0", "#E7ECF0"
RB_BG = (0x0D, 0x11, 0x15)      # Road Buddy page background (COL_BG)
RB_MAX = (240, 164)             # Road Buddy: space between header and footer

font = TTFont(HERE / "poppins-latin-700-normal.woff")
glyphs, cmap = font.getGlyphSet(), font.getBestCmap()
UPM = font["head"].unitsPerEm


def glyph(ch, x, color, size):
    pen = SVGPathPen(glyphs)
    g = glyphs[cmap[ord(ch)]]
    g.draw(pen)
    s = size / UPM
    return (f'<path transform="translate({x:.2f},0) scale({s:.5f},{-s:.5f})" '
            f'd="{pen.getCommands()}" fill="{color}"/>'), g.width * s


def glyph_bounds(ch):
    pen = BoundsPen(glyphs)
    glyphs[cmap[ord(ch)]].draw(pen)
    return pen.bounds


def wordmark(dark, size=100, track=-2):
    """Returns (svg body with baseline at y=0, width, top y)."""
    ink = LIGHT if dark else NAVY
    parts, x, o_x = [], 0.0, 0.0
    for ch, col in (("v", ink), ("a", ink), ("n", ink), ("n", BLUE), ("o", BLUE), ("d", BLUE), ("e", BLUE)):
        p, adv = glyph(ch, x, col, size)
        parts.append(p)
        if ch == "o":
            o_x = x
        x += adv + track
    s = size / UPM
    xmin, ymin, xmax, ymax = glyph_bounds("o")
    cx, cy, top = o_x + (xmin + xmax) / 2 * s, -(ymin + ymax) / 2 * s, -ymax * s
    dot, node, stem = size * 0.05, size * 0.075, size * 0.055
    up = top - size * 0.13
    parts.append(
        f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{dot:.2f}" fill="{BLUE}"/>'
        f'<g stroke="{BLUE}" stroke-width="{stem:.2f}" stroke-linecap="round">'
        f'<line x1="{cx:.2f}" y1="{top + stem:.2f}" x2="{cx:.2f}" y2="{up:.2f}"/>'
        f'<line x1="{cx + size * 0.17:.2f}" y1="{top + size * 0.08:.2f}" '
        f'x2="{cx + size * 0.27:.2f}" y2="{top - size * 0.05:.2f}"/></g>'
        f'<circle cx="{cx:.2f}" cy="{up - node * 0.6:.2f}" r="{node:.2f}" fill="{BLUE}"/>'
        f'<circle cx="{cx + size * 0.29:.2f}" cy="{top - size * 0.08:.2f}" r="{node:.2f}" fill="{BLUE}"/>')
    return "".join(parts), x - track, up - node * 1.6


def symbol():
    """200 x 200: pointy-top hexagon, van side view with network, short antenna."""
    return f'''<polygon points="100,8 182,54 182,146 100,192 18,146 18,54" fill="{NAVY}" stroke="{BLUE}" stroke-width="9" stroke-linejoin="round"/>
<path d="M42 132 V96 Q42 82 56 81 H128 Q140 81 147 90 L162 111 Q166 116 166 123 V132 Z" fill="{LIGHT}"/>
<path d="M131 89 H139 L153 110 H131 Z" fill="{NAVY}"/>
<circle cx="70" cy="134" r="13" fill="{LIGHT}" stroke="{NAVY}" stroke-width="5"/>
<circle cx="140" cy="134" r="13" fill="{LIGHT}" stroke="{NAVY}" stroke-width="5"/>
<g stroke="{BLUE}" stroke-width="4"><line x1="60" y1="110" x2="86" y2="96"/><line x1="86" y1="96" x2="112" y2="113"/><line x1="60" y1="110" x2="112" y2="113"/></g>
<g fill="{BLUE}"><circle cx="60" cy="110" r="7"/><circle cx="86" cy="96" r="7"/><circle cx="112" cy="113" r="7"/></g>
<line x1="100" y1="81" x2="100" y2="70" stroke="{LIGHT}" stroke-width="4.5"/>
<circle cx="100" cy="66" r="6.5" fill="{BLUE}"/>
<path d="M88 58 Q100 46 112 58" fill="none" stroke="{BLUE}" stroke-width="4.5" stroke-linecap="round"/>
<path d="M80 50 Q100 30 120 50" fill="none" stroke="{BLUE}" stroke-width="4.5" stroke-linecap="round" opacity=".6"/>'''


def svg(body, w, h, x=0.0, y=0.0):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.0f}" height="{h:.0f}" '
            f'viewBox="{x:.1f} {y:.1f} {w:.1f} {h:.1f}">{body}</svg>\n')


def horizontal(dark):
    wm, ww, top = wordmark(dark)
    y = 100 - top / 2 - 4                       # wordmark vertically centered next to the symbol
    w = 225 + ww + 10
    return svg(symbol() + f'<g transform="translate(225,{y:.1f})">{wm}</g>', w, 200)


def stacked(dark):
    """Symbol above the wordmark (Road Buddy)."""
    wm, ww, top = wordmark(dark)
    w = max(200, ww) + 20
    body = (f'<g transform="translate({(w - 200) / 2:.1f},0)">{symbol()}</g>'
            f'<g transform="translate({(w - ww) / 2:.1f},{215 - top:.1f})">{wm}</g>')
    return svg(body, w, 215 - top + 8)


def png(svg_text, path=None, width=None):
    data = cairosvg.svg2png(bytestring=svg_text.encode(), output_width=width)
    if path:
        Path(path).write_bytes(data)
    return Image.open(BytesIO(data)).convert("RGBA")


def write_rgb565(img, path):
    flat = Image.new("RGB", img.size, RB_BG)
    flat.paste(img, mask=img.getchannel("A"))
    raw = flat.tobytes()
    vals = [((raw[i] & 0xF8) << 8) | ((raw[i + 1] & 0xFC) << 3) | (raw[i + 2] >> 3)
            for i in range(0, len(raw), 3)]
    rows = [", ".join(f"0x{v:04X}" for v in vals[i:i + 12]) for i in range(0, len(vals), 12)]
    path.write_text(
        "// VanNode logo for the start page, generated by tools/logo/make_logo.py - do not edit\n"
        "#pragma once\n#include <stdint.h>\n\n"
        f"#define LOGO_W {img.size[0]}\n#define LOGO_H {img.size[1]}\n\n"
        "// RGB565, drawn on the page background color\n"
        "static const uint16_t LOGO_IMG[LOGO_W * LOGO_H] PROGMEM = {\n  "
        + ",\n  ".join(rows) + "\n};\n", encoding="utf-8")


def data_uri(svg_text):
    """Compact SVG as data URI for HTML/CSS (double quotes become single quotes)."""
    compact = " ".join(svg_text.replace('"', "'").split())
    return "data:image/svg+xml," + quote(compact, safe=" '=:/,.;-()")


def write_web_blocks(symbol_svg):
    uri = data_uri(symbol_svg)
    COMMON_WEB.mkdir(parents=True, exist_ok=True)
    (COMMON_WEB / "vannode.css").write_text(
        "/* VanNode symbol left of the page title, generated by tools/logo/make_logo.py */\n"
        f'.vn{{display:inline-block;width:26px;height:26px;flex:none;vertical-align:-6px;'
        f'background:url("{uri}") center/contain no-repeat}}\n', encoding="utf-8")
    (COMMON_WEB / "vannode-icon.html").write_text(
        f'<link rel="icon" type="image/svg+xml" href="{uri}">\n', encoding="utf-8")


def c_array(name, data, per_line=16):
    rows = [", ".join(f"0x{b:02X}" for b in data[i:i + per_line]) for i in range(0, len(data), per_line)]
    return f"static const uint8_t {name}[] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def lv_image(name, data_name, w, h, cf, bpp):
    return (f"const lv_image_dsc_t {name} = {{\n"
            f"  .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = {cf}, .flags = 0, "
            f".w = {w}, .h = {h}, .stride = {w * bpp}, .reserved_2 = 0 }},\n"
            f"  .data_size = sizeof({data_name}),\n  .data = {data_name},\n  .reserved = NULL,\n}};\n")


def write_panel_images(symbol_svg):
    # header: ARGB8888 with transparency (the header turns into a warning banner)
    hdr = png(symbol_svg, width=PANEL_HDR * 4).resize((PANEL_HDR, PANEL_HDR), Image.LANCZOS)
    px = hdr.tobytes()
    argb = bytearray()
    for i in range(0, len(px), 4):
        argb += bytes((px[i + 2], px[i + 1], px[i], px[i + 3]))   # LVGL ARGB8888 = B, G, R, A in memory
    # QR code: RGB565 on white with a margin, so the code stays readable around it
    inner = PANEL_QR - 6
    q = png(symbol_svg, width=inner * 4).resize((inner, inner), Image.LANCZOS)
    qr = Image.new("RGB", (PANEL_QR, PANEL_QR), (255, 255, 255))
    qr.paste(q, (3, 3), q)
    px = qr.tobytes()
    rgb = bytearray()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        rgb += bytes((v & 0xFF, v >> 8))               # RGB565 little endian
    PANEL_IMG.write_text(
        "// VanNode symbol for the wall panel, generated by tools/logo/make_logo.py - do not edit\n"
        '#include "lvgl.h"\n\n'
        + c_array("vn_logo_hdr_data", argb) + "\n"
        + lv_image("vn_logo_hdr", "vn_logo_hdr_data", PANEL_HDR, PANEL_HDR, "LV_COLOR_FORMAT_ARGB8888", 4) + "\n"
        + c_array("vn_logo_qr_data", rgb) + "\n"
        + lv_image("vn_logo_qr", "vn_logo_qr_data", PANEL_QR, PANEL_QR, "LV_COLOR_FORMAT_RGB565", 2),
        encoding="utf-8")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "vannode-logo.svg": horizontal(False),
        "vannode-logo-dark.svg": horizontal(True),
        "vannode-symbol.svg": svg(symbol(), 200, 200),
    }
    for dark in (False, True):
        wm, ww, top = wordmark(dark)
        files[f"vannode-wordmark{'-dark' if dark else ''}.svg"] = svg(wm, ww + 16, -top + 14, -8, top - 8)
    for name, text in files.items():
        (OUT / name).write_text(text, encoding="utf-8")
    png(files["vannode-logo.svg"], OUT / "vannode-logo.png", 1200)
    png(files["vannode-logo-dark.svg"], OUT / "vannode-logo-dark.png", 1200)
    png(files["vannode-symbol.svg"], OUT / "vannode-symbol.png", 512)

    write_web_blocks(files["vannode-symbol.svg"])
    write_panel_images(files["vannode-symbol.svg"])

    rb = png(stacked(True), width=600)
    rb.thumbnail(RB_MAX, Image.LANCZOS)
    write_rgb565(rb, RB_HEADER)
    print(f"docs/logo: {len(files)} SVG + 3 PNG, Road Buddy {rb.size[0]}x{rb.size[1]}, "
          f"panel images, web blocks (run tools/common/sync_common.py next)")


if __name__ == "__main__":
    main()
