#!/usr/bin/env python3
"""
spectra6_convert.py

Convert an image into packed 4bpp indexed data for E Ink Spectra 6 panels
driven by Seeed_GFX2's pushImage4BPP().

Output is a C header containing a uint8_t array named gImage_7inch3 by
default, so it drops straight into the Seeed_GFX2 Bitmap example.

    pip install Pillow

Examples
--------
Basic, replacing the example's asset:
    python spectra6_convert.py photo.jpg -o gImage_7inch3.h

Save a raw binary instead (for streaming from onboard flash):
    python spectra6_convert.py photo.jpg --raw photo.bin

Generate a calibration pattern FIRST to discover your panel's index values:
    python spectra6_convert.py --calibrate -o calib.h

Preview what the quantised result will look like:
    python spectra6_convert.py photo.jpg -o img.h --preview preview.png
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow")


WIDTH, HEIGHT = 800, 480
BYTES_EXPECTED = WIDTH * HEIGHT // 2  # 192000


# ---------------------------------------------------------------------------
# PALETTE  --  EDIT THIS TO MATCH YOUR PANEL
# ---------------------------------------------------------------------------
# Maps a 4-bit index value to the approximate sRGB appearance of that pigment.
#
# The index VALUES are the part you must verify. They are not standardised
# across vendors. From my training data, Waveshare's Spectra 6 driver uses
#   0x0 black, 0x1 white, 0x2 yellow, 0x3 red, 0x5 blue, 0x6 green
# (note the unused 0x4 and 0x7) but this could NOT be confirmed for
# Seeed_GFX2. Run --calibrate first and read the real mapping off the panel.
#
# The RGB values are only used for nearest-colour matching during
# quantisation. They do not travel to the display. Tune them if a colour
# keeps winning when it shouldn't.
PALETTE = {
    0x0: (255, 255, 255),    # white
    0x1: (0, 0, 0),          # black
    0x2: (58, 126, 72),      # green
    0x6: (186, 49, 57),      # red
    0xb:  (255, 214, 61),     # yellow
    0xd:  (39, 68, 156),      # blue
}

PALETTE_OLD = {
    0x0: (0, 0, 0),          # black
    0x1: (255, 255, 255),    # white
    0x2: (255, 214, 61),     # yellow
    0x3: (186, 49, 57),      # red
    0x5: (39, 68, 156),      # blue
    0x6: (58, 126, 72),      # green
}

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------

def _to_linear(c):
    """sRGB byte -> linear light 0..1 (gamma 2.2 approximation)."""
    return (c / 255.0) ** 2.2


def _to_srgb(v):
    """Linear light -> sRGB byte."""
    v = max(0.0, min(1.0, v))
    return (v ** (1.0 / 2.2)) * 255.0


PAL_KEYS = sorted(PALETTE.keys())
PAL_SRGB = {k: PALETTE[k] for k in PAL_KEYS}
PAL_LIN = {k: tuple(_to_linear(c) for c in PALETTE[k]) for k in PAL_KEYS}

_memo = {}


def nearest_index(r, g, b, linear=False):
    """Closest palette index to an sRGB triple."""
    key = (r, g, b, linear)
    hit = _memo.get(key)
    if hit is not None:
        return hit

    if linear:
        tr, tg, tb = _to_linear(r), _to_linear(g), _to_linear(b)
        table = PAL_LIN
    else:
        tr, tg, tb = float(r), float(g), float(b)
        table = PAL_SRGB

    best_k, best_d = PAL_KEYS[0], float("inf")
    for k in PAL_KEYS:
        pr, pg, pb = table[k]
        d = (tr - pr) ** 2 + (tg - pg) ** 2 + (tb - pb) ** 2
        if d < best_d:
            best_d, best_k = d, k

    _memo[key] = best_k
    return best_k


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def fit_image(img, mode):
    """Scale/crop to exactly WIDTH x HEIGHT."""
    img = img.convert("RGB")
    tw, th = WIDTH, HEIGHT

    if mode == "stretch":
        return img.resize((tw, th), Image.LANCZOS)

    sw, sh = img.size
    scale = max(tw / sw, th / sh) if mode == "cover" else min(tw / sw, th / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    img = img.resize((nw, nh), Image.LANCZOS)

    if mode == "cover":
        left, top = (nw - tw) // 2, (nh - th) // 2
        return img.crop((left, top, left + tw, top + th))

    # contain: letterbox on white, which is the panel's resting state
    canvas = Image.new("RGB", (tw, th), (255, 255, 255))
    canvas.paste(img, ((tw - nw) // 2, (th - nh) // 2))
    return canvas


# ---------------------------------------------------------------------------
# Quantisation
# ---------------------------------------------------------------------------

def quantise(img, dither=True, serpentine=True, linear=False):
    """
    Reduce to palette indices, returning a list of rows of index values.

    Floyd-Steinberg error diffusion. Serpentine scanning alternates the
    direction of each row, which suppresses the diagonal "worm" artefacts
    that plain FS produces on large flat areas.
    """
    w, h = img.size
    px = img.load()

    # Work in floats so accumulated error is not clipped prematurely.
    buf = [[[float(c) for c in px[x, y]] for x in range(w)] for y in range(h)]
    out = [[0] * w for _ in range(h)]

    for y in range(h):
        forward = (not serpentine) or (y % 2 == 0)
        xs = range(w) if forward else range(w - 1, -1, -1)
        step = 1 if forward else -1

        for x in xs:
            r, g, b = buf[y][x]
            ri = int(max(0, min(255, r + 0.5)))
            gi = int(max(0, min(255, g + 0.5)))
            bi = int(max(0, min(255, b + 0.5)))

            idx = nearest_index(ri, gi, bi, linear)
            out[y][x] = idx

            if not dither:
                continue

            pr, pg, pb = PAL_SRGB[idx]
            er, eg, eb = r - pr, g - pg, b - pb

            def spread(xx, yy, weight):
                if 0 <= xx < w and 0 <= yy < h:
                    cell = buf[yy][xx]
                    cell[0] += er * weight
                    cell[1] += eg * weight
                    cell[2] += eb * weight

            spread(x + step, y, 7 / 16)
            spread(x - step, y + 1, 3 / 16)
            spread(x, y + 1, 5 / 16)
            spread(x + step, y + 1, 1 / 16)

    return out


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------

def pack_4bpp(rows, high_first=True):
    """Two pixels per byte. high_first puts the LEFT pixel in the high nibble."""
    data = bytearray()
    for row in rows:
        for x in range(0, WIDTH, 2):
            left = row[x] & 0x0F
            right = (row[x + 1] & 0x0F) if x + 1 < WIDTH else 0
            if high_first:
                data.append((left << 4) | right)
            else:
                data.append((right << 4) | left)
    return bytes(data)


# ---------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------

def write_header(path, data, name):
    lines = [
        "// Generated by spectra6_convert.py",
        f"// {WIDTH}x{HEIGHT}, packed 4bpp indexed (two pixels per byte)",
        f"// {len(data)} bytes",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"const uint8_t {name}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    Path(path).write_text("\n".join(lines))


def write_preview(path, rows):
    img = Image.new("RGB", (WIDTH, HEIGHT))
    px = img.load()
    for y, row in enumerate(rows):
        for x, idx in enumerate(row):
            px[x, y] = PAL_SRGB[idx]
    img.save(path)


def calibration_rows():
    """16 horizontal bands, band i filled with raw index value i."""
    band_h = HEIGHT // 16
    rows = []
    for y in range(HEIGHT):
        idx = min(15, y // band_h)
        rows.append([idx] * WIDTH)
    return rows


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Convert an image to packed 4bpp for E Ink Spectra 6.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("image", nargs="?", help="source image (png/jpg/bmp/...)")
    ap.add_argument("-o", "--out", help="output C header path")
    ap.add_argument("--raw", help="also/instead write raw packed bytes here")
    ap.add_argument("--name", default="myImage",
                    help="C array name (default: myImage)")
    ap.add_argument("--fit", choices=["contain", "cover", "stretch"],
                    default="contain",
                    help="how to reach 800x480 (default: contain)")
    ap.add_argument("--no-dither", action="store_true",
                    help="disable Floyd-Steinberg dithering")
    ap.add_argument("--no-serpentine", action="store_true",
                    help="scan every row left-to-right")
    ap.add_argument("--linear", action="store_true",
                    help="match colours in linear light instead of sRGB")
    ap.add_argument("--low-first", action="store_true",
                    help="put the left pixel in the LOW nibble")
    ap.add_argument("--preview", help="write a PNG of the quantised result")
    ap.add_argument("--calibrate", action="store_true",
                    help="emit a 16-band test pattern instead of an image")
    args = ap.parse_args()

    if args.calibrate:
        rows = calibration_rows()
        data = pack_4bpp(rows, high_first=not args.low_first)
        if args.out:
            write_header(args.out, data, args.name)
            print(f"wrote {args.out}  ({len(data)} bytes)")
        if args.raw:
            Path(args.raw).write_bytes(data)
            print(f"wrote {args.raw}  ({len(data)} bytes)")
        if args.preview:
            write_preview(args.preview, rows)
            print(f"wrote {args.preview}")
        print()
        print("Flash this, then read the panel top-to-bottom.")
        print("Band N occupies rows %d-%d." % (0, HEIGHT // 16 - 1))
        print("Whatever colour band N shows IS index N. Update PALETTE")
        print("to match, then re-run for your real image.")
        return

    if not args.image:
        ap.error("provide an image, or use --calibrate")

    src = Image.open(args.image)
    fitted = fit_image(src, args.fit)
    rows = quantise(
        fitted,
        dither=not args.no_dither,
        serpentine=not args.no_serpentine,
        linear=args.linear,
    )
    data = pack_4bpp(rows, high_first=not args.low_first)

    if len(data) != BYTES_EXPECTED:
        sys.exit(f"internal error: got {len(data)} bytes, expected {BYTES_EXPECTED}")

    wrote = False
    if args.out:
        write_header(args.out, data, args.name)
        print(f"wrote {args.out}  ({len(data)} bytes)")
        wrote = True
    if args.raw:
        Path(args.raw).write_bytes(data)
        print(f"wrote {args.raw}  ({len(data)} bytes)")
        wrote = True
    if args.preview:
        write_preview(args.preview, rows)
        print(f"wrote {args.preview}")
        wrote = True

    if not wrote:
        sys.exit("nothing written: pass -o, --raw or --preview")

    print()
    print("Sketch side:")
    print(f"    #include \"{Path(args.out).name if args.out else 'image.h'}\"")
    print(f"    display.pushImage4BPP(0, 0, {WIDTH}, {HEIGHT}, "
          f"{args.name}, true);")


if __name__ == "__main__":
    main()
