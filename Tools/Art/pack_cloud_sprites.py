"""Cloud sprites for the stylized sky's horizon towers.

The towers on the horizon are painted cloud cut-outs, lit by the sky like the rest of the clouds (the way stylised
games draw their skies): the painting's light and shade become a light mask the sky colours by time of day.

    python pack_cloud_sprites.py clean <sprite_dir> <name>=<painting.png> [...]
        Turns paintings with a transparent background into sprites: keeps the largest piece (drops specks the
        painter left around it), fills holes, crops, scales to at most 1024 px high. The name's prefix says the
        kind: fc_ fair-weather cumulus, tc_ towering cumulus, cb_ cumulonimbus.

    python pack_cloud_sprites.py pack <sprite_dir> <atlas.png> <include.ush>
        Packs every sprite in <sprite_dir> into one RGBA atlas (linear data, not colour):
            R = light (0 the deepest shade in the painting .. 1 its lit tops)
            G = distance inside the outline, 0 at the edge .. 1 a little way in (the rim light)
            B = distance inside the outline over a tenth of the sprite's height (thick cores)
            A = coverage
        and writes the include the sky shader reads the sprites' rectangles from.

Needs numpy, scipy and Pillow.
"""
import math
import pathlib
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

KINDS = {"fc": 0, "tc": 1, "cb": 2}
KIND_NAMES = ["fair-weather cumulus", "towering cumulus", "cumulonimbus"]
ATLAS = 2048
COLUMNS = 2
ROWS = 3
GUTTER = 16


def kind_of(name):
    prefix = name.split("_", 1)[0]
    if prefix not in KINDS:
        raise SystemExit(f"{name}: the name must start with one of {', '.join(k + '_' for k in KINDS)}")
    return KINDS[prefix]


def clean(sprite_dir, pairs):
    out = pathlib.Path(sprite_dir)
    out.mkdir(parents=True, exist_ok=True)
    for pair in pairs:
        name, path = pair.split("=", 1)
        kind_of(name)
        rgba = np.asarray(Image.open(path).convert("RGBA")).astype(np.float32) / 255.0
        alpha = rgba[..., 3]
        solid = alpha > 0.5
        labels, count = ndimage.label(solid)
        if count == 0:
            raise SystemExit(f"{path}: nothing opaque")
        sizes = ndimage.sum(solid, labels, range(1, count + 1))
        largest = labels == (int(np.argmax(sizes)) + 1)
        keep = ndimage.binary_fill_holes(largest)
        # Keep the anti-aliased rim of the kept piece, nothing else; holes inside it become solid.
        alpha = np.where(ndimage.binary_dilation(keep, iterations=2), alpha, 0.0)
        alpha = np.where(keep & ~largest, 1.0, alpha)
        rgba[..., 3] = alpha
        ys, xs = np.nonzero(alpha > 0.0)
        y0, y1 = max(ys.min() - 4, 0), min(ys.max() + 5, alpha.shape[0])
        x0, x1 = max(xs.min() - 4, 0), min(xs.max() + 5, alpha.shape[1])
        img = Image.fromarray((rgba[y0:y1, x0:x1] * 255.0 + 0.5).astype(np.uint8), "RGBA")
        if img.height > 1024:
            img = img.resize((round(img.width * 1024 / img.height), 1024), Image.LANCZOS)
        img.save(out / f"{name}.png")
        print(f"{name}: {img.width}x{img.height} from {path} ({count - 1} specks dropped)")


def sprite_channels(img):
    rgba = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    alpha = rgba[..., 3]
    solid = alpha > 0.5
    lum = rgba[..., 0] * 0.2126 + rgba[..., 1] * 0.7152 + rgba[..., 2] * 0.0722
    lo, hi = np.percentile(lum[solid], [1.0, 99.5])
    light = np.clip((lum - lo) / max(hi - lo, 1e-3), 0.0, 1.0)
    # The rim's colour is half background: take the light of the nearest solid pixel instead.
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    light = light[iy, ix]
    inside = ndimage.distance_transform_edt(solid)
    height = img.height
    rim = np.clip(inside / (0.025 * height), 0.0, 1.0)
    core = np.clip(inside / (0.1 * height), 0.0, 1.0)
    return np.stack([light, rim, core, alpha], axis=-1)


def pack(sprite_dir, atlas_path, include_path):
    paths = sorted(pathlib.Path(sprite_dir).glob("*.png"), key=lambda p: (kind_of(p.stem), p.stem))
    if not paths or len(paths) > COLUMNS * ROWS:
        raise SystemExit(f"{sprite_dir}: need 1..{COLUMNS * ROWS} sprites, found {len(paths)}")
    atlas = np.zeros((ATLAS, ATLAS, 4), np.float32)
    cell_w = ATLAS // COLUMNS
    cell_h = ATLAS // ROWS
    rects, aspects, kinds = [], [], []
    for index, path in enumerate(paths):
        img = Image.open(path)
        aspect = img.width / img.height
        scale = min((cell_w - 2 * GUTTER) / img.width, (cell_h - 2 * GUTTER) / img.height)
        w, h = max(1, int(img.width * scale)), max(1, int(img.height * scale))
        channels = sprite_channels(img.resize((w, h), Image.LANCZOS))
        col, row = index % COLUMNS, index // COLUMNS
        x0 = col * cell_w + (cell_w - w) // 2
        y0 = row * cell_h + cell_h - GUTTER - h     # sprites stand on the bottom of their cell
        atlas[y0:y0 + h, x0:x0 + w] = channels
        rects.append((x0 / ATLAS, y0 / ATLAS, (x0 + w) / ATLAS, (y0 + h) / ATLAS))
        aspects.append(aspect)
        kinds.append(kind_of(path.stem))
        print(f"{path.stem}: {KIND_NAMES[kinds[-1]]}, {w}x{h} at ({x0}, {y0}), aspect {aspect:.3f}")
    Image.fromarray((atlas * 255.0 + 0.5).astype(np.uint8), "RGBA").save(atlas_path)

    first = [kinds.index(k) if k in kinds else 0 for k in range(3)]
    count = [kinds.count(k) for k in range(3)]
    lines = [
        "// Generated by Tools/Art/pack_cloud_sprites.py from SourceArt/Sky/Clouds - do not edit by hand.",
        "//",
        "// The horizon cloud sprites in T_DreamCloudSprites: each sprite's rectangle in the atlas (u0, v0, u1, v1;",
        "// it stands on its bottom edge), its width over its height, and where each kind starts and how many there",
        "// are (0 fair-weather cumulus, 1 towering cumulus, 2 cumulonimbus).",
        "",
        "#ifndef DREAMSKY_CLOUD_SPRITES_USH",
        "#define DREAMSKY_CLOUD_SPRITES_USH",
        "",
        f"#define DSKY_SPRITE_COUNT {len(paths)}",
        "static const float4 DSky_SpriteRect[DSKY_SPRITE_COUNT] = {",
        *[f"\tfloat4({r[0]:.6f}, {r[1]:.6f}, {r[2]:.6f}, {r[3]:.6f}),   // {p.stem}" for r, p in zip(rects, paths)],
        "};",
        "static const float DSky_SpriteAspect[DSKY_SPRITE_COUNT] = { " + ", ".join(f"{a:.4f}" for a in aspects) + " };",
        "static const int DSky_SpriteFirst[3] = { " + ", ".join(str(f) for f in first) + " };",
        "static const int DSky_SpriteCountOf[3] = { " + ", ".join(str(c) for c in count) + " };",
        "",
        "#endif // DREAMSKY_CLOUD_SPRITES_USH",
        "",
    ]
    pathlib.Path(include_path).write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"atlas {atlas_path}, include {include_path}")


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "clean":
        clean(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) == 5 and sys.argv[1] == "pack":
        pack(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        print(__doc__)
        sys.exit(2)
