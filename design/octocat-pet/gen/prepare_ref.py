#!/usr/bin/env python3
"""Cut the generated Octocat base off its magenta background."""

from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent


def main() -> None:
    im = Image.open(ROOT / "base-flat.png").convert("RGB")
    a = np.asarray(im).astype(np.float32)
    bg = np.array([245.0, 15.0, 245.0], dtype=np.float32)
    dist = np.linalg.norm(a - bg, axis=2)
    alpha = np.clip((dist - 45.0) / 75.0, 0.0, 1.0)

    # Despill magenta fringes: pull blue down toward green on semi-transparent edges.
    edge = (alpha > 0.02) & (alpha < 0.98)
    a[edge, 2] = np.minimum(a[edge, 2], a[edge, 1])

    rgba = np.dstack([a, alpha * 255.0]).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    bbox = img.getbbox()
    if not bbox:
        raise SystemExit("no foreground found")
    img = img.crop(bbox)

    # Square canvas with a small margin, then resize to 512 for image references.
    side = int(max(img.size) * 1.08)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.alpha_composite(img, ((side - img.width) // 2, (side - img.height) // 2))
    square.resize((512, 512), Image.LANCZOS).save(ROOT / "base-cutout.png")

    ref = Image.new("RGB", (512, 512), (255, 0, 255))
    ref.paste(square.resize((512, 512), Image.LANCZOS), (0, 0), square.resize((512, 512), Image.LANCZOS))
    ref.save(ROOT / "base-ref.png")
    print(ROOT / "base-cutout.png", ROOT / "base-ref.png")


if __name__ == "__main__":
    main()
