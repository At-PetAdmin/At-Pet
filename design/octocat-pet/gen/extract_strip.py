#!/usr/bin/env python3
"""Extract evenly spaced poses from a generated magenta sprite strip."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 160, 192


def cutout(image: Image.Image, key: tuple[float, float, float] = (242.0, 10.0, 242.0)) -> Image.Image:
    rgb = np.asarray(image.convert("RGB")).astype(np.float32)
    dist = np.linalg.norm(rgb - np.array(key, dtype=np.float32), axis=2)
    alpha = np.clip((dist - 55.0) / 70.0, 0.0, 1.0)
    # Kill the magenta halo that survives a plain distance key.
    magenta_strength = np.minimum(rgb[:, :, 0] - rgb[:, :, 1], rgb[:, :, 2] - rgb[:, :, 1])
    alpha *= np.clip(1.0 - (magenta_strength - 8.0) / 35.0, 0.0, 1.0)
    # Remove magenta contamination from every edge pixel, including fully opaque ones.
    magenta = (rgb[:, :, 2] > rgb[:, :, 1] + 12) & (rgb[:, :, 0] > rgb[:, :, 1] + 12)
    rgb[magenta, 2] = rgb[magenta, 1]
    rgb[magenta, 0] = np.minimum(rgb[magenta, 0], rgb[magenta, 1] + 45)
    rgba = np.dstack([rgb, alpha * 255.0]).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def pose_ranges(alpha: np.ndarray, count: int) -> list[tuple[int, int]]:
    col = (alpha > 80).any(axis=0)
    runs = []
    start = None
    for i, value in enumerate(col):
        if value and start is None:
            start = i
        elif not value and start is not None:
            if i - start > 4:
                runs.append((start, i - 1))
            start = None
    if start is not None:
        runs.append((start, len(col) - 1))
    if len(runs) != count:
        width = alpha.shape[1] / count
        return [(int(i * width), int((i + 1) * width) - 1) for i in range(count)]
    return runs


def shadow_layer() -> Image.Image:
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(layer)
    draw.ellipse((39, 173, 121, 187), fill=(126, 170, 194, 46))
    draw.ellipse((49, 176, 111, 185), fill=(126, 170, 194, 38))
    return layer


def extract(path: Path, count: int, target_height: int, preserve_baseline: bool,
            out_dir: Path, prefix: str, baseline: int = 176,
            shared_scale: bool = False, center: str = "head", margin: int = 4) -> list[Path]:
    strip = cutout(Image.open(path))
    alpha = np.asarray(strip.getchannel("A"))
    boxes = []
    for x0, x1 in pose_ranges(alpha, count):
        sub = alpha[:, x0:x1 + 1]
        ys, xs = np.where(sub > 20)
        if len(xs) == 0:
            raise SystemExit(f"empty pose in {path.name}")
        boxes.append((x0 + int(xs.min()), int(ys.min()), x0 + int(xs.max()) + 1, int(ys.max()) + 1))

    heights = [b[3] - b[1] for b in boxes]
    ref_h = float(np.median(heights))
    global_bottom = max(b[3] for b in boxes)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for i, box in enumerate(boxes):
        pose = strip.crop(box)
        scale = target_height / ref_h if shared_scale else target_height / (box[3] - box[1])
        pose = pose.resize((max(1, round(pose.width * scale)), max(1, round(pose.height * scale))),
                           Image.LANCZOS)
        fit = min(1.0, (W - 2 * margin) / pose.width, (H - 2 * margin) / pose.height)
        if fit < 1.0:
            pose = pose.resize((max(1, round(pose.width * fit)), max(1, round(pose.height * fit))),
                               Image.LANCZOS)
            scale *= fit
        cell = shadow_layer()
        # Center on the head, not the full silhouette: the tail swings and would
        # otherwise make the whole body jitter sideways.
        head_bottom = box[1] + max(1, int((box[3] - box[1]) * 0.58))
        head_sub = alpha[box[1]:head_bottom, box[0]:box[2]]
        hys, hxs = np.where(head_sub > 20)
        head_center = box[0] + (int(hxs.min()) + int(hxs.max())) / 2.0
        left = int(round(80 - (head_center - box[0]) * scale)) if center == "head" else (W - pose.width) // 2
        left = max(margin, min(left, W - pose.width - margin))
        bottom = baseline - (global_bottom - box[3]) * scale if preserve_baseline else baseline
        top = int(round(bottom - pose.height))
        top = max(margin, min(top, H - pose.height - margin))
        cell.alpha_composite(pose, (left, top))
        dest = out_dir / f"{prefix}-{i:02d}.png"
        cell.save(dest)
        paths.append(dest)
    return paths


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("strip")
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--height", type=int, default=150)
    ap.add_argument("--preserve-baseline", action="store_true")
    ap.add_argument("--shared-scale", action="store_true")
    ap.add_argument("--center", choices=["head", "bbox"], default="head")
    ap.add_argument("--margin", type=int, default=4)
    ap.add_argument("--out", required=True)
    ap.add_argument("--prefix", required=True)
    args = ap.parse_args()
    paths = extract(Path(args.strip), args.count, args.height, args.preserve_baseline,
                    Path(args.out), args.prefix, shared_scale=args.shared_scale,
                    center=args.center, margin=args.margin)
    print(f"{len(paths)} frames -> {args.out}")


if __name__ == "__main__":
    main()
