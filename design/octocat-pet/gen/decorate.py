#!/usr/bin/env python3
"""Add deterministic flat props (Zzz, hearts) to extracted frames."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def heart(draw: ImageDraw.ImageDraw, x: float, y: float, size: float, color=(244, 91, 105, 235)) -> None:
    r = size * 0.28
    draw.ellipse((x - size * 0.5, y - size * 0.45, x - size * 0.5 + 2 * r, y - size * 0.45 + 2 * r), fill=color)
    draw.ellipse((x + size * 0.5 - 2 * r, y - size * 0.45, x + size * 0.5, y - size * 0.45 + 2 * r), fill=color)
    draw.polygon([(x - size * 0.5, y - size * 0.1), (x + size * 0.5, y - size * 0.1), (x, y + size * 0.62)], fill=color)


def z(draw: ImageDraw.ImageDraw, x: float, y: float, size: float, color=(143, 211, 244, 235)) -> None:
    w = size
    draw.line((x, y, x + w, y), fill=color, width=max(2, int(size * 0.22)))
    draw.line((x + w, y, x, y + w), fill=color, width=max(2, int(size * 0.22)))
    draw.line((x, y + w, x + w, y + w), fill=color, width=max(2, int(size * 0.22)))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", choices=["sleep", "interact"], required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--dir", default=str(Path(__file__).resolve().parent / "frames"))
    args = ap.parse_args()
    root = Path(args.dir)
    for i in range(args.count):
        path = root / f"{args.prefix}-{i:02d}.png"
        img = Image.open(path).convert("RGBA")
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        if args.kind == "sleep":
            z(draw, 120, 60 - i * 1.5, 7)
            z(draw, 130, 44 - i * 2.0, 8)
            z(draw, 141, 26 - i * 2.5, 9)
        else:
            heart(draw, 124, 52 - 3 * (i % 2), 12)
            heart(draw, 136, 36 - 3 * ((i + 1) % 2), 8)
        Image.alpha_composite(img, layer).save(path)


if __name__ == "__main__":
    main()
