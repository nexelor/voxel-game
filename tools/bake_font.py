#!/usr/bin/env python3
"""
bake_font.py — Converts any .ttf/.otf font into the engine's
BitmapFont.hpp format (engine/ui/BitmapFont.hpp).

This is how the debug-UI bitmap font is generated.  Rather than
hand-transcribing pixel data (error-prone — see the git history
for proof), this script renders each ASCII glyph (32-126) from a
real font file, supersamples for clean edges, thresholds to 1-bit,
and emits a verified uint8_t[FONT_COUNT][8] table.

USAGE
-----
    pip install --break-system-packages pillow
    python3 tools/bake_font.py /path/to/Monocraft.ttf

    # Optional flags:
    python3 tools/bake_font.py /path/to/Monocraft.ttf \
        --size 39 --threshold 90 --supersample 4 \
        --out engine/ui/BitmapFont.hpp

For Minecraft's actual look, use Monocraft (SIL OFL 1.1, free for
commercial use, redistribution and modification):
    https://github.com/IdreesInc/Monocraft/releases/latest

Download Monocraft.ttf (or extract from the .ttc bundle) and run:
    python3 tools/bake_font.py Monocraft.ttf

If glyphs look too cramped/clipped (e.g. descenders on g/j/p/q/y get
cut off), try a smaller --size or tweak --y-offset.
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("This script requires Pillow: pip install --break-system-packages pillow")
    sys.exit(1)

CHAR_W, CHAR_H = 8, 8
FIRST, LAST = 32, 126

ESCAPE_NAMES = {
    '\\': 'backslash',
    '"':  'dquote',
    "'":  'squote',
}


def find_best_size(font_path: str, target_h: int, max_search: int = 200) -> int:
    """Binary-search-ish scan for a point size whose 'M' cap height
    roughly fills the supersampled cell without overflowing."""
    best = 8
    for size in range(8, max_search):
        font = ImageFont.truetype(font_path, size)
        bbox = font.getbbox("M")
        h = bbox[3] - bbox[1]
        if h <= target_h - 4:
            best = size
        else:
            break
    return best


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, big_w: int, big_h: int,
                  ascent: int, y_offset: int) -> Image.Image:
    img = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(img)
    bbox = font.getbbox(ch)
    if bbox is None:
        return img
    w = bbox[2] - bbox[0]
    x = (big_w - w) // 2 - bbox[0]
    y = (big_h - ascent) // 2 + y_offset
    draw.text((x, y), ch, font=font, fill=255)
    return img


def downsample_threshold(img: Image.Image, cell_w: int, cell_h: int, thresh: int):
    small = img.resize((cell_w, cell_h), Image.BOX)
    px = small.load()
    rows = []
    for yy in range(cell_h):
        byte = 0
        for xx in range(cell_w):
            bit = 1 if px[xx, yy] > thresh else 0
            byte |= (bit << (7 - xx))
        rows.append(byte)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("font_path", help="Path to .ttf/.otf font file")
    ap.add_argument("--size", type=int, default=None,
                     help="Point size to render at (default: auto-detect)")
    ap.add_argument("--threshold", type=int, default=90,
                     help="0-255 brightness threshold for 1-bit conversion (default: 90)")
    ap.add_argument("--supersample", type=int, default=4,
                     help="Supersample factor before box-downsampling (default: 4)")
    ap.add_argument("--y-offset", type=int, default=0,
                     help="Shift glyphs vertically in supersampled space (default: 0)")
    ap.add_argument("--out", default="engine/ui/BitmapFont.hpp",
                     help="Output header path (default: engine/ui/BitmapFont.hpp)")
    ap.add_argument("--font-name", default=None,
                     help="Display name for the header comment (default: filename)")
    args = ap.parse_args()

    font_path = Path(args.font_path)
    if not font_path.exists():
        print(f"Font file not found: {font_path}")
        sys.exit(1)

    ss = args.supersample
    big_w, big_h = CHAR_W * ss, CHAR_H * ss

    size = args.size or find_best_size(str(font_path), big_h)
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()

    print(f"Font:        {font_path.name}")
    print(f"Point size:  {size}")
    print(f"Supersample: {ss}x  ({big_w}x{big_h} -> {CHAR_W}x{CHAR_H})")
    print(f"Threshold:   {args.threshold}")
    print(f"Ascent/descent: {ascent}/{descent}")

    entries = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        if ch == ' ':
            rows = [0, 0, 0, 0, 0, 0, 0, 0]
        else:
            img = render_glyph(font, ch, big_w, big_h, ascent, args.y_offset)
            rows = downsample_threshold(img, CHAR_W, CHAR_H, args.threshold)
        entries.append((code, ch, rows))

    font_name = args.font_name or font_path.stem

    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("// ─────────────────────────────────────────────")
    lines.append("//  BitmapFont")
    lines.append("//")
    lines.append(f"//  An 8×8 pixel bitmap font for ASCII characters")
    lines.append(f"//  32–126 (space through tilde).")
    lines.append("//")
    lines.append("//  Each character is 8 rows of 8 bits packed")
    lines.append("//  into a uint8_t[8].  Bit 7 of each row is")
    lines.append("//  the leftmost pixel (MSB-first).")
    lines.append("//")
    lines.append(f"//  Baked from: {font_name}")
    lines.append(f"//  via tools/bake_font.py (supersample={ss}x, "
                  f"size={size}pt, threshold={args.threshold})")
    lines.append("//")
    lines.append("//  Regenerate with:")
    lines.append(f"//    python3 tools/bake_font.py path/to/{font_path.name}")
    lines.append("//")
    lines.append("//  No external file dependency at runtime —")
    lines.append("//  fully baked into this header.")
    lines.append("// ─────────────────────────────────────────────")
    lines.append("")
    lines.append("static constexpr int FONT_CHAR_W = 8;")
    lines.append("static constexpr int FONT_CHAR_H = 8;")
    lines.append("static constexpr int FONT_FIRST  = 32;   // space")
    lines.append("static constexpr int FONT_LAST   = 126;  // ~")
    lines.append("static constexpr int FONT_COUNT  = FONT_LAST - FONT_FIRST + 1;")
    lines.append("")
    lines.append("// Each entry: 8 bytes, one per row, MSB = left pixel.")
    lines.append("static constexpr uint8_t FONT_BITMAP[FONT_COUNT][8] = {")

    for code, ch, rows in entries:
        label = ESCAPE_NAMES.get(ch, ch if ch != ' ' else '')
        hexrow = ",".join(f"0x{b:02X}" for b in rows)
        lines.append(f"    // {code} {label}")
        lines.append(f"    {{ {hexrow} }},")

    lines.append("};")
    lines.append("")
    lines.append("// Returns a pointer to the 8 bitmap rows for ASCII character c.")
    lines.append("// Returns the '?' glyph for out-of-range characters.")
    lines.append("inline const uint8_t* FontGlyph(char c) {")
    lines.append("    const int idx = static_cast<int>(static_cast<unsigned char>(c)) - FONT_FIRST;")
    lines.append("    if (idx < 0 || idx >= FONT_COUNT) {")
    lines.append("        // '?' = index of 63-32 = 31")
    lines.append("        return FONT_BITMAP[31];")
    lines.append("    }")
    lines.append("    return FONT_BITMAP[idx];")
    lines.append("}")
    lines.append("")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines))

    print(f"\nWrote {len(entries)} glyphs to {out_path}")
    print("\nPreview (A, F, M, 0):")
    for code, ch, rows in entries:
        if ch in "AFM0":
            print(f"--- '{ch}' ---")
            for r in rows:
                print(format(r, '08b').replace('0', '.').replace('1', '#'))


if __name__ == "__main__":
    main()
