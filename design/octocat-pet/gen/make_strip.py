#!/usr/bin/env python3
"""Compose extracted frames into a labeled QA strip."""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main() -> None:
    out = Path(sys.argv[1])
    files = [Path(p) for p in sys.argv[2:]]
    scale = 2
    cell_w, cell_h = 160 * scale, 192 * scale + 18
    canvas = Image.new("RGB", (cell_w * len(files), cell_h), (244, 245, 247))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12)
    except OSError:
        font = ImageFont.load_default()
    for i, path in enumerate(files):
        img = Image.open(path).convert("RGBA").resize((160 * scale, 192 * scale), Image.LANCZOS)
        bg = Image.new("RGB", img.size, (244, 245, 247))
        bg.paste(img, (0, 0), img)
        canvas.paste(bg, (i * cell_w, 0))
        draw.rectangle([i * cell_w, 192 * scale, (i + 1) * cell_w, cell_h], fill=(226, 229, 233))
        draw.text((i * cell_w + 5, 192 * scale + 2), path.stem, fill=(60, 64, 70), font=font)
    canvas.save(out)
    print(out)


if __name__ == "__main__":
    main()
