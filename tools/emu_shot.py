"""Combine the game's own screen dumps (sdmc:/sonic3ds_top.ppm and _bottom.ppm, written when
sonic3ds.cfg contains 'p') into one PNG laid out like a 3DS.

usage: python emu_shot.py <sdmc dir> <out.png>
"""
import os
import sys

from PIL import Image


def main():
    sd, out = sys.argv[1], sys.argv[2]
    top = Image.open(os.path.join(sd, "sonic3ds_top.ppm")).convert("RGB")
    bottom = Image.open(os.path.join(sd, "sonic3ds_bottom.ppm")).convert("RGB")
    canvas = Image.new("RGB", (400, 480), (0, 0, 0))
    canvas.paste(top, (0, 0))
    canvas.paste(bottom, (40, 240))
    canvas = canvas.resize((800, 960), Image.NEAREST)
    canvas.save(out)
    print(out)


if __name__ == "__main__":
    main()
