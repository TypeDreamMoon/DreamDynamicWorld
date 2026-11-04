"""Tiles screenshots into one labelled contact sheet: make_contact_sheet.py <out.png> <shot.png>...

Each tile is labelled with the part of its file name after the last underscore (DreamWorld.ShotTour names
shots <Prefix>_HHMM.png, so the label is the hour). Needs Pillow; exits 2 without it.
"""

import math
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Pillow is not installed; no contact sheet.")
    sys.exit(2)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    out, paths = argv[1], argv[2:]
    shots = [Image.open(p).convert("RGB") for p in paths]
    tile_w = 480
    tile_h = int(tile_w * shots[0].height / shots[0].width)
    columns = min(4, len(shots))
    rows = math.ceil(len(shots) / columns)
    sheet = Image.new("RGB", (columns * tile_w, rows * tile_h), (16, 16, 16))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("arial.ttf", 22)
    except OSError:
        font = ImageFont.load_default()
    for i, (path, shot) in enumerate(zip(paths, shots)):
        x, y = (i % columns) * tile_w, (i // columns) * tile_h
        sheet.paste(shot.resize((tile_w, tile_h)), (x, y))
        label = os.path.splitext(os.path.basename(path))[0].rsplit("_", 1)[-1]
        if len(label) == 4 and label.isdigit():
            label = label[:2] + ":" + label[2:]
        draw.rectangle((x + 6, y + 6, x + 86, y + 36), fill=(0, 0, 0))
        draw.text((x + 12, y + 9), label, fill=(255, 255, 255), font=font)
    sheet.save(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
