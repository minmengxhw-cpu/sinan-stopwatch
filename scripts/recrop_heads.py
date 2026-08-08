#!/usr/bin/env python3
"""Make fidelity-preserving, round-safe dog-head assets from supplied images."""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

CANVAS = 536
SUBJECT = 410


def crop_with_padding(image: Image.Image, cx: int, cy: int, radius: int) -> Image.Image:
    side = radius * 2
    left, top = cx - radius, cy - radius
    square = Image.new("RGB", (side, side), "black")
    src_left, src_top = max(0, left), max(0, top)
    src_right, src_bottom = min(image.width, left + side), min(image.height, top + side)
    if src_right > src_left and src_bottom > src_top:
        part = image.crop((src_left, src_top, src_right, src_bottom))
        square.paste(part, (src_left - left, src_top - top))
    return square


def make_asset(source: pathlib.Path, crop: dict, output: pathlib.Path) -> None:
    image = Image.open(source).convert("RGB")
    square = crop_with_padding(image, crop["cx"], crop["cy"], crop["r"])
    if crop.get("rotate"):
        square = square.rotate(crop["rotate"], resample=Image.Resampling.BICUBIC)
    subject = square.resize((SUBJECT, SUBJECT), Image.Resampling.LANCZOS)
    subject = ImageEnhance.Color(subject).enhance(1.03)

    mask = Image.new("L", (SUBJECT, SUBJECT), 0)
    ImageDraw.Draw(mask).ellipse((7, 7, SUBJECT - 7, SUBJECT - 7), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(7))

    canvas = Image.new("RGB", (CANVAS, CANVAS), "black")
    offset = (CANVAS - SUBJECT) // 2
    canvas.paste(subject, (offset, offset), mask)
    canvas.save(output, quality=94, optimize=True)


def make_contact(old_dir: pathlib.Path, new_dir: pathlib.Path, output: pathlib.Path) -> None:
    thumb = 230
    label_h = 30
    gap = 12
    sheet = Image.new("RGB", (thumb * 4 + gap * 5, (thumb * 2 + label_h) * 4 + gap * 5), "#090909")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index in range(16):
        row, col = divmod(index, 4)
        x = gap + col * (thumb + gap)
        y = gap + row * (thumb * 2 + label_h + gap)
        name = f"{index + 1:02d}.jpg"
        old = Image.open(old_dir / name).convert("RGB").resize((thumb, thumb), Image.Resampling.LANCZOS)
        new = Image.open(new_dir / name).convert("RGB").resize((thumb, thumb), Image.Resampling.LANCZOS)
        sheet.paste(old, (x, y))
        sheet.paste(new, (x, y + thumb + label_h))
        draw.text((x + 4, y + thumb + 8), f"{name}  old / new", fill="#d3aa64", font=font)
    sheet.save(output, quality=94)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--crops", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        raise RuntimeError("output directory already exists; choose a new dated directory")

    crops = json.loads(args.crops.read_text())
    manifest = json.loads((args.source / "manifest.json").read_text())
    args.out.mkdir(parents=True)
    for item in manifest["items"]:
        name = item["file"]
        make_asset(args.source / name, crops[name], args.out / name)
        item["mode"] = "disc"
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
    )
    shutil.copy2(args.source / "glyph.png", args.out / "glyph.png")
    shutil.copy2(args.crops, args.out / "head-crops.json")
    make_contact(args.source, args.out, args.out / "old-new-contact.jpg")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
