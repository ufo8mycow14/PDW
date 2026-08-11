from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def load_font(size: int) -> ImageFont.ImageFont:
    candidates = [
        Path(r"C:\Windows\Fonts\segoeui.ttf"),
        Path(r"C:\Windows\Fonts\arial.ttf"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


def build_contact_sheet(
    images: list[Path],
    output: Path,
    *,
    columns: int,
    thumb_width: int,
    label_height: int,
) -> None:
    if not images:
        raise ValueError("No images were supplied")
    font = load_font(max(12, label_height // 2))
    rendered: list[tuple[Image.Image, str]] = []
    max_cell_height = 0
    for image_path in images:
        with Image.open(image_path) as source:
            image = source.convert("RGB")
            ratio = thumb_width / image.width
            resized = image.resize(
                (thumb_width, max(1, round(image.height * ratio))),
                Image.Resampling.LANCZOS,
            )
        rendered.append((resized, image_path.stem))
        max_cell_height = max(max_cell_height, resized.height + label_height)

    rows = math.ceil(len(rendered) / columns)
    gap = 18
    margin = 24
    sheet_width = margin * 2 + columns * thumb_width + (columns - 1) * gap
    sheet_height = margin * 2 + rows * max_cell_height + (rows - 1) * gap
    sheet = Image.new("RGB", (sheet_width, sheet_height), "#E8EBEF")
    draw = ImageDraw.Draw(sheet)
    for index, (image, label) in enumerate(rendered):
        row, column = divmod(index, columns)
        x = margin + column * (thumb_width + gap)
        y = margin + row * (max_cell_height + gap)
        draw.rounded_rectangle(
            (x - 3, y - 3, x + thumb_width + 3, y + max_cell_height - 3),
            radius=7,
            fill="white",
            outline="#B7BEC8",
            width=1,
        )
        sheet.paste(image, (x, y))
        draw.text((x + 6, y + image.height + 5), label, fill="#16202A", font=font)
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output, format="PNG", optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a labelled PNG contact sheet.")
    parser.add_argument("input_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--glob", default="*.png")
    parser.add_argument("--columns", type=int, default=3)
    parser.add_argument("--thumb-width", type=int, default=420)
    parser.add_argument("--label-height", type=int, default=38)
    parser.add_argument("--start", type=int)
    parser.add_argument("--end", type=int)
    args = parser.parse_args()
    images = sorted(
        args.input_dir.glob(args.glob),
        key=lambda path: [
            int(part) if part.isdigit() else part.lower()
            for part in re.split(r"(\d+)", path.name)
        ],
    )
    if args.start is not None or args.end is not None:
        first = args.start if args.start is not None else 1
        last = args.end if args.end is not None else 10**9
        images = [
            path
            for path in images
            if (match := re.search(r"(\d+)", path.stem))
            and first <= int(match.group(1)) <= last
        ]
    build_contact_sheet(
        images,
        args.output,
        columns=args.columns,
        thumb_width=args.thumb_width,
        label_height=args.label_height,
    )


if __name__ == "__main__":
    main()
