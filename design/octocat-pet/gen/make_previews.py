#!/usr/bin/env python3
"""Build animated GIF and size-check previews for the generated pet."""

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from build_sheet import LAYOUT, W, H


ROOT = Path(__file__).resolve().parent
PET = ROOT.parents[2] / "assets" / "default" / "pet.json"


def font(size: int):
    for name in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main() -> None:
    pet = json.loads(PET.read_text(encoding="utf-8"))
    for name, indices in LAYOUT.items():
        fps = pet["animations"][name].get("fps", pet["fps"])
        frames = []
        for n in range(len(indices)):
            img = Image.open(ROOT / "frames" / f"{name}-{n:02d}.png").convert("RGBA")
            bg = Image.new("RGB", (W, H), (246, 247, 248))
            bg.paste(img, (0, 0), img)
            frames.append(bg.resize((W * 2, H * 2), Image.LANCZOS))
        frames[0].save(ROOT / f"preview-{name}.gif", save_all=True,
                       append_images=frames[1:], duration=round(1000 / fps), loop=0,
                       disposal=2, optimize=False)

    img = Image.open(ROOT / "frames" / "idle-00.png").convert("RGBA")
    img = img.resize((200, 240), Image.LANCZOS)
    out = Image.new("RGB", (440, 300), (255, 255, 255))
    light = Image.new("RGB", (200, 240), (246, 247, 248))
    light.paste(img, (0, 0), img)
    dark = Image.new("RGB", (200, 240), (28, 30, 34))
    dark.paste(img, (0, 0), img)
    out.paste(light, (10, 40))
    out.paste(dark, (230, 40))
    d = ImageDraw.Draw(out)
    d.text((10, 12), "Octocat at 200x240", fill=(40, 44, 50), font=font(13))
    out.save(ROOT / "size-check.png")
    print(ROOT)


if __name__ == "__main__":
    main()
