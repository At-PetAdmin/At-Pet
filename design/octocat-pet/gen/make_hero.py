#!/usr/bin/env python3
"""Compose a compact hero sheet for the bundled Octocat pet."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent


def font(size: int, bold: bool = False):
    names = ["C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf",
             "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf"]
    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main() -> None:
    out = Image.new("RGB", (760, 520), (247, 248, 250))
    d = ImageDraw.Draw(out)
    d.text((28, 22), "Octocat", fill=(28, 32, 38), font=font(26, True))
    d.text((28, 56), "bundled default pet  ·  160×192 cells  ·  flat vector", fill=(104, 112, 122), font=font(12))

    idle = Image.open(ROOT / "frames" / "idle-00.png").convert("RGBA")
    idle = idle.resize((200, 240), Image.LANCZOS)
    panel = Image.new("RGB", (230, 300), (255, 255, 255))
    panel.paste(idle, (15, 30), idle)
    out.paste(panel, (28, 92))
    d.rectangle((28, 92, 258, 392), outline=(225, 228, 233))
    d.text((40, 360), "idle", fill=(90, 98, 108), font=font(11))

    states = [("walk", "walk-00"), ("tool", "tool-00"), ("done", "done-02"),
              ("sleep", "sleep-00"), ("interact", "interact-00"), ("permission", "permission-00")]
    x0, y0 = 286, 96
    for i, (label, name) in enumerate(states):
        img = Image.open(ROOT / "frames" / f"{name}.png").convert("RGBA")
        img = img.resize((160, 192), Image.LANCZOS)
        x = x0 + (i % 3) * 150
        y = y0 + (i // 3) * 195
        cell = Image.new("RGB", (140, 170), (255, 255, 255))
        cell.paste(img.resize((133, 160), Image.LANCZOS), (3, 2), img.resize((133, 160), Image.LANCZOS))
        out.paste(cell, (x, y))
        d.rectangle((x, y, x + 139, y + 169), outline=(225, 228, 233))
        d.text((x + 8, y + 151), label, fill=(90, 98, 108), font=font(11))

    d.text((28, 480), "idle 8 · walk 6 · tool 6 · done 6 · sleep 4 · interact 4", fill=(120, 128, 138), font=font(11))
    d.text((28, 500), "thinking 3 · error 3 · cancelled 3 · permission 3", fill=(120, 128, 138), font=font(11))
    out.save(ROOT / "hero.png")
    print(ROOT / "hero.png")


if __name__ == "__main__":
    main()
