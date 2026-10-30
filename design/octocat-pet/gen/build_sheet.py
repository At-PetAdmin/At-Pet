#!/usr/bin/env python3
"""Assemble the image-generated frames into the bundled 12 x 7 atlas."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 160, 192
COLS, ROWS = 12, 7

LAYOUT = {
    "idle": list(range(0, 8)),
    "walk": list(range(12, 18)),
    "sleep": list(range(24, 28)),
    "interact": list(range(28, 32)),
    "tool": list(range(36, 42)),
    "done": list(range(48, 54)),
    "work": list(range(60, 64)),
    "review": list(range(64, 68)),
    "thinking": list(range(72, 75)),
    "error": list(range(75, 78)),
    "cancelled": list(range(78, 81)),
    "permission": list(range(81, 84)),
}


def font(size: int):
    for name in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main() -> None:
    root = Path(__file__).resolve().parent
    frames = root / "frames"
    sheet = Image.new("RGBA", (W * COLS, H * ROWS), (0, 0, 0, 0))
    for name, indices in LAYOUT.items():
        for n, idx in enumerate(indices):
            path = frames / f"{name}-{n:02d}.png"
            img = Image.open(path).convert("RGBA")
            if img.size != (W, H):
                raise SystemExit(f"{path} is {img.size}")
            bbox = img.getbbox()
            if bbox and (bbox[0] <= 0 or bbox[1] <= 0 or bbox[2] >= W or bbox[3] >= H):
                print(f"warning: {name}-{n:02d} touches the cell edge: {bbox}")
            sheet.alpha_composite(img, ((idx % COLS) * W, (idx // COLS) * H))
    out = root / "spritesheet.png"
    sheet.save(out)

    label_h = 20
    contact = Image.new("RGB", (sheet.width, sheet.height + ROWS * label_h), (246, 247, 248))
    contact.paste(sheet, (0, 0), sheet)
    draw = ImageDraw.Draw(contact)
    f = font(12)
    labels = ["idle (8)", "walk (6)", "sleep (4) / interact (4)", "tool (6)", "done (6)",
              "work (4) / review (4)", "thinking / error / cancelled / permission (3 each)"]
    for row, label in enumerate(labels):
        y = (row + 1) * H + row * label_h
        draw.rectangle([0, y, contact.width, y + label_h], fill=(232, 234, 237))
        draw.text((8, y + 3), label, fill=(60, 64, 70), font=f)
    contact.save(root / "contact-sheet.png")
    print(f"sheet: {out} ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
