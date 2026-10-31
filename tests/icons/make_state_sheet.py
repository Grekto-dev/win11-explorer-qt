#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Arrange real QIcon test renders and measure/crop optional reference PNGs.

Requires Pillow. Reference crops are evidence only, never application resources.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rendered_dir", type=Path)
    parser.add_argument("--reference-dir", type=Path)
    args = parser.parse_args()
    output = args.rendered_dir
    names = {"cut": 322, "copy": 370, "paste": 418, "rename": 466, "delete": 562}
    sizes = (16, 20, 24, 32)
    sheet = Image.new("RGB", (1160, 500), "white")
    draw = ImageDraw.Draw(sheet)
    columns = [(mode, size) for mode in ("Normal", "Disabled") for size in sizes]
    for i, (mode, size) in enumerate(columns):
        draw.text((150 + i * 125, 10), f"{mode} {size}px", fill="black")
    files = []
    for row, name in enumerate(names):
        y = 55 + row * 85
        draw.text((8, y), name, fill="black")
        if name == "paste":
            draw.text((8, y + 18), "Normal: unobserved", fill="black")
        for col, (mode, size) in enumerate(columns):
            path = output / f"{name}-{mode}-{size}.png"
            image = Image.open(path).convert("RGBA")
            if image.size != (size, size) or not image.getbbox():
                raise ValueError(f"Invalid test render: {path}")
            # Native resolution at top, nearest-neighbor 2x below for inspection.
            x = 155 + col * 125
            sheet.paste(image, (x, y - 10), image)
            enlarged = image.resize((size * 2, size * 2), Image.Resampling.NEAREST)
            sheet.paste(enlarged, (x + 40, y - 10), enlarged)
            files.append(path.name)
    sheet.save(output / "toolbar-states-contact-sheet.png")
    report = {"render_count": len(files), "renders": files, "client_origin": [194, 162],
              "screen_centers": names, "screen_command_y_approx": 274,
              "paste_normal_observed": False, "references": {}}
    if args.reference_dir:
        for state in ("empty", "selected"):
            image = Image.open(args.reference_dir / f"toolbar-{state}.png").convert("RGB")
            image.crop((296, 258, 582, 291)).resize((1144, 132), Image.Resampling.NEAREST).save(
                output / f"reference-{state}-toolbar-4x.png")
            report["references"][state] = {}
            for name, center in names.items():
                box = (center - 10, 264, center + 10, 285)
                crop = image.crop(box)
                crop.resize((160, 168), Image.Resampling.NEAREST).save(
                    output / f"reference-{state}-{name}-8x.png")
                colors = Counter(crop.getpixel((x, y)) for y in range(crop.height) for x in range(crop.width))
                report["references"][state][name] = {
                    "box_screen": box,
                    "nonwhite_colors": [(rgb, n) for rgb, n in colors.most_common() if rgb != (255, 255, 255)][:30],
                }
    (output / "reference-pixels.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(files)} real renders; sheet: {(output / 'toolbar-states-contact-sheet.png').resolve()}")


if __name__ == "__main__":
    main()
