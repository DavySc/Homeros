#!/usr/bin/env python3
"""Renders the icon files from the two SVGs; run it after changing either
(python3 assets/icons.py, needs cairosvg and Pillow) and commit what it writes.
The outputs are committed so the build needs neither."""
import io, os
import cairosvg
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# Below about 48 px the traced drawing turns to mush, so 20 to 40 px come
# from a simplified drawing and 16 px is placed by hand, one char a pixel.
PX16 = """
..BBBBBBBBBBBB..
.BBBBBBBBBBByBB.
BrBBBBBBrBBBBByB
BrrBBBBrrBBBBBBB
BrrrrrrrrBBBBBBB
BrrkrrkrrBBBBBBB
BrrrrrrrrggBBggB
BrrrrrrrrgkggkgB
BrrrrrrrrggooggB
BOOrrOOOOOOOOOOB
BOmmmmmmmmmMMMOB
BOmMmmmmmmmMMMOB
BOmmMmmmmmmMmMOB
BOmMmMMmmmmmmmOB
.OmmmmmmmmmmmmO.
..OmmmmmmmmmmO..
""".split()
# Catppuccin Mocha, as in the SVGs
PAL = {"B": "1e1e2e", "k": "11111b", "r": "f5e0dc", "g": "a6e3a1", "o": "fab387",
       "O": "6c7086", "m": "181825", "M": "cba6f7", "y": "f9e2af"}


def render(n):
    if n == 16:
        im = Image.new("RGBA", (16, 16))
        for y, row in enumerate(PX16):
            for x, ch in enumerate(row):
                if ch != ".":
                    im.putpixel((x, y), tuple(bytes.fromhex(PAL[ch])) + (255,))
        return im
    svg = "homeros-small.svg" if n <= 40 else "homeros.svg"
    png = cairosvg.svg2png(url=os.path.join(HERE, svg), output_width=n, output_height=n)
    return Image.open(io.BytesIO(png)).convert("RGBA")


# Windows picks from these by DPI: 16/20/24 small icons, 32/40/48 large, 256 Explorer
ICO = [16, 20, 24, 32, 40, 48, 64, 256]
ims = [render(n) for n in ICO]
ims[-1].save(os.path.join(HERE, "homeros.ico"), sizes=[(n, n) for n in ICO], append_images=ims[:-1])

# freedesktop hicolor theme; larger sizes scale assets/homeros.svg
for n in (16, 24, 32, 48):
    d = os.path.join(HERE, "hicolor", f"{n}x{n}", "apps")
    os.makedirs(d, exist_ok=True)
    render(n).save(os.path.join(d, "homeros.png"), optimize=True)
