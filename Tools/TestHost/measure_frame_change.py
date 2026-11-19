"""How much the sky changes from one frame to the next, and where it jumps.

    measure_frame_change.py [--top 0.5] [--plot out.png] [--strip out.png] [--json out.json] <frame.png>...

The frames are a sequence (DreamWorld.Script 'frames' saves <name>_fNNNN.png; they are sorted by name). For every
pair of consecutive frames it takes the mean absolute difference of the top part of the picture (--top: the
fraction of rows from the top, the sky when the view looks up a little), in 8-bit levels averaged over R, G, B.

A sky that drifts and a camera that moves change every frame by about the same amount; a jump is a frame whose
change stands out from its neighbours. A step counts as a jump when it is more than --ratio (3) times the median of
the steps around it (--window frames either side) and more than --floor (0.5 levels).

Prints one summary line per sequence (median, 95th percentile and largest step, the largest step over its local
median, and the jumps), --plot draws the steps as bars (jumps in red), --strip tiles the frames around the largest
step side by side with their difference. Needs Pillow and NumPy. Exit codes: 0 no jumps, 3 jumps found, 2 bad input.
"""

import argparse
import json
import os
import sys

try:
    import numpy as np
    from PIL import Image, ImageDraw
except ImportError:
    print("needs Pillow and NumPy")
    sys.exit(2)


def load(path, top):
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    rows = max(1, int(round(image.shape[0] * top)))
    return image[:rows]


def steps_of(paths, top):
    previous = load(paths[0], top)
    steps = []
    for path in paths[1:]:
        current = load(path, top)
        if current.shape != previous.shape:
            raise ValueError(f"{path}: size differs from the frame before it")
        steps.append(float(np.mean(np.abs(current - previous))))
        previous = current
    return np.array(steps)


def jumps_of(steps, window, ratio, floor):
    local = np.empty_like(steps)
    for i in range(len(steps)):
        lo, hi = max(0, i - window), min(len(steps), i + window + 1)
        around = np.concatenate([steps[lo:i], steps[i + 1:hi]])
        local[i] = np.median(around) if len(around) else steps[i]
    over = steps / np.maximum(local, 1.0e-3)
    jumps = [i for i in range(len(steps)) if steps[i] > floor and over[i] > ratio]
    return local, over, jumps


def plot(path, steps, jumps, title):
    width, height = max(400, 6 * len(steps) + 60), 240
    image = Image.new("RGB", (width, height), (24, 24, 24))
    draw = ImageDraw.Draw(image)
    top = max(float(np.max(steps)), 1.0e-3)
    for i, step in enumerate(steps):
        x = 40 + i * 6
        y = height - 30 - int((height - 60) * step / top)
        draw.rectangle([x, y, x + 4, height - 30], fill=(220, 60, 60) if i in jumps else (120, 170, 220))
    draw.text((8, 6), f"{title}  max {top:.2f} levels", fill=(230, 230, 230))
    draw.text((8, height - 22), "frame step ->", fill=(160, 160, 160))
    image.save(path)


def strip(path, paths, worst, top):
    names = paths[max(0, worst - 1):worst + 3]
    frames = [Image.open(p).convert("RGB") for p in names]
    w, h = frames[0].size
    scale = min(1.0, 480.0 / w)
    tw, th = int(w * scale), int(h * scale)
    a = np.asarray(Image.open(paths[worst]).convert("RGB"), dtype=np.float32)
    b = np.asarray(Image.open(paths[worst + 1]).convert("RGB"), dtype=np.float32)
    diff = np.clip(np.abs(b - a) * 8.0, 0, 255).astype(np.uint8)
    tiles = frames + [Image.fromarray(diff)]
    sheet = Image.new("RGB", (tw * len(tiles), th + 18), (16, 16, 16))
    draw = ImageDraw.Draw(sheet)
    for i, tile in enumerate(tiles):
        sheet.paste(tile.resize((tw, th)), (i * tw, 18))
        label = os.path.splitext(os.path.basename(names[i]))[0][-5:] if i < len(names) else f"|diff| x8 {worst}->{worst + 1}"
        draw.text((i * tw + 4, 3), label, fill=(230, 230, 230))
    draw.line([(0, 18 + int(th * top)), (sheet.width, 18 + int(th * top))], fill=(255, 200, 0))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("frames", nargs="+")
    parser.add_argument("--top", type=float, default=0.5)
    parser.add_argument("--window", type=int, default=4)
    parser.add_argument("--ratio", type=float, default=3.0)
    parser.add_argument("--floor", type=float, default=0.5)
    parser.add_argument("--skip", type=int, default=0, help="leading frames to leave out (a camera still settling)")
    parser.add_argument("--plot")
    parser.add_argument("--strip")
    parser.add_argument("--json")
    parser.add_argument("--label")
    args = parser.parse_args()

    paths = sorted(args.frames)[args.skip:]
    if len(paths) < 3:
        print("need at least three frames")
        return 2
    try:
        steps = steps_of(paths, args.top)
    except (OSError, ValueError) as error:
        print(error)
        return 2
    local, over, jumps = jumps_of(steps, args.window, args.ratio, args.floor)
    worst = int(np.argmax(over))
    label = args.label or os.path.basename(paths[0]).rsplit("_f", 1)[0]
    print(f"{label}: {len(paths)} frames  step median {np.median(steps):.2f}  p95 {np.percentile(steps, 95):.2f}  "
          f"max {np.max(steps):.2f}  worst/local {over[worst]:.1f}x at {worst}->{worst + 1}  jumps {len(jumps)}"
          + (f" at {', '.join(str(j) for j in jumps[:12])}" if jumps else ""))
    if args.plot:
        plot(args.plot, steps, jumps, label)
    if args.strip:
        strip(args.strip, paths, min(worst, len(paths) - 2), args.top)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"label": label, "frames": len(paths), "steps": [round(float(s), 4) for s in steps],
                       "median": float(np.median(steps)), "p95": float(np.percentile(steps, 95)),
                       "max": float(np.max(steps)), "worstOverLocal": float(over[worst]), "worst": worst,
                       "jumps": jumps}, out, indent=1)
    return 3 if jumps else 0


if __name__ == "__main__":
    sys.exit(main())
