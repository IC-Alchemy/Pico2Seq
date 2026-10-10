#!/usr/bin/env python3
"""Render the docs/display demo images from the firmware's own renderer.

The screens come from tests/tools/lane_display_demo.cpp, which replays
src/display/LaneDisplay.h exactly as the Pico does. This script only converts
those PPM rasters to PNG and composites the comparison sheets.

Usage (from the repository root):
    python scripts/render_display_demos.py
    python scripts/render_display_demos.py --skip-build
    python scripts/render_display_demos.py --cxx "C:/Program Files/LLVM/bin/clang++.exe"
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "display"
BUILD = ROOT / "build_test_ninja"
# Intermediates live in the build tree: only the PNGs are repository content.
RAW = BUILD / "display_demo_raw"

# Geometry mirrored from src/display/LaneDisplay.h (header/rows are the renderer's).
HEADER_HEIGHT = 68
CELL_HEIGHT = 48
WIDTH, HEIGHT = 320, 480

STYLES = ["TRACE", "STAIR", "BARS", "DOTS", "HEAT", "RIBBON", "ORBIT", "TICKS"]

BACKGROUND = (6, 8, 8)
INK = (232, 236, 236)
MUTED = (134, 140, 140)
ACCENT = (120, 232, 250)


def font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # Pillow < 10
        return ImageFont.load_default()


def build_demo(cxx: str | None) -> None:
    configure = ["cmake", "-B", str(BUILD), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug"]
    if cxx:
        configure += [f"-DCMAKE_CXX_COMPILER={cxx}", "-DCMAKE_CXX_FLAGS=-D_USE_MATH_DEFINES"]
    subprocess.run(configure, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(BUILD), "--target", "pico2seq_display_demo"],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def run_demo() -> None:
    exe = BUILD / "tests" / "pico2seq_display_demo.exe"
    if not exe.exists():
        exe = BUILD / "tests" / "pico2seq_display_demo"
    # Clear intermediates first: a stale raster would otherwise be converted into a
    # misleading "current" image for a screen the demo no longer renders.
    if RAW.exists():
        for stale in RAW.glob("*.ppm"):
            stale.unlink()
    RAW.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), str(RAW)], cwd=ROOT, check=True)


def convert() -> dict[str, Image.Image]:
    images: dict[str, Image.Image] = {}
    for ppm in sorted(RAW.glob("*.ppm")):
        image = Image.open(ppm).convert("RGB")
        if image.size != (WIDTH, HEIGHT):
            raise SystemExit(f"{ppm.name}: unexpected size {image.size}")
        image.save(OUT / f"{ppm.stem}.png")
        images[ppm.stem] = image
    # Drop PNGs whose source screen no longer exists, so the folder stays honest.
    for png in OUT.glob("*.png"):
        if png.stem not in images and not png.stem.endswith(("comparison", "overview",
                                                            "map")):
            png.unlink()
    return images


def caption(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, size: int = 18,
            colour: tuple[int, int, int] = INK) -> None:
    draw.text(xy, text, font=font(size), fill=colour)


def style_comparison(images: dict[str, Image.Image]) -> None:
    """The same two lanes in all eight encodings, enlarged, one per row."""
    crop_top = HEADER_HEIGHT
    crop_bottom = HEADER_HEIGHT + 2 * CELL_HEIGHT
    scale = 2
    gutter = 220
    row_height = (crop_bottom - crop_top) * scale
    sheet = Image.new("RGB", (gutter + WIDTH * scale, len(STYLES) * (row_height + 18) + 92),
                      BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    caption(draw, (20, 20), "THE SAME TWO LANES, EIGHT ENCODINGS", 26)
    caption(draw, (20, 54), "V1 NOTE 16 STEPS + V2 VELOCITY 6 STEPS, LOOP START 0", 16, MUTED)
    y = 92
    for style in STYLES:
        image = images.get(f"matrix-bank1-{style}")
        if image is None:
            continue
        strip = image.crop((0, crop_top, WIDTH, crop_bottom)).resize(
            (WIDTH * scale, row_height), Image.NEAREST)
        sheet.paste(strip, (gutter, y))
        caption(draw, (20, y + 8), style, 22, ACCENT)
        note = {
            "TRACE": "polyline, dots at steps",
            "STAIR": "sample and hold",
            "BARS": "one bar per step",
            "DOTS": "radius is level",
            "HEAT": "brightness is level",
            "RIBBON": "mirrored thickness",
            "ORBIT": "angle is time",
            "TICKS": "quantized ruler",
        }[style]
        caption(draw, (20, y + 34), note, 15, MUTED)
        y += row_height + 18
    sheet.save(OUT / "style-comparison.png")


def layout_map(images: dict[str, Image.Image]) -> None:
    """Where each region of the new screen sits, annotated on a real frame."""
    base = images["matrix-bank1-TRACE"]
    scale = 2
    sheet = Image.new("RGB", (WIDTH * scale + 300, HEIGHT * scale), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    sheet.paste(base.resize((WIDTH * scale, HEIGHT * scale), Image.NEAREST), (0, 0))
    notes = [
        (10, "header: page, voice pair, tempo, bank and style", HEADER_HEIGHT),
        (HEADER_HEIGHT + 2, "16 cells: 2 voices x 8 lanes, 48px each", 2 * CELL_HEIGHT),
        (HEADER_HEIGHT + 2 * CELL_HEIGHT, "left column follows the pad bank's first voice", 2 * CELL_HEIGHT),
        (452, "footer: gesture help and the three marks every style keeps", 28),
    ]
    for index, (top, text, height) in enumerate(notes):
        box_top = top * scale
        box_bottom = (top + height) * scale - 1
        draw.rectangle([8, box_top, WIDTH * scale - 9, box_bottom], outline=ACCENT if index == 0 else MUTED)
        draw.text((WIDTH * scale + 14, box_top + (box_bottom - box_top) // 2 - 8), text,
                  font=font(15), fill=INK)
        draw.line([WIDTH * scale - 9, box_top, WIDTH * scale + 10, box_top], fill=ACCENT)
    sheet.save(OUT / "layout-map.png")


def pages_overview(images: dict[str, Image.Image]) -> None:
    """The two new screens, plus the preserved contextual page, side by side."""
    panels = [
        ("matrix-bank1-TRACE", "LANE MATRIX - bank 1 (8 lanes)"),
        ("matrix-bank2-trace", "LANE MATRIX - bank 2 (Decay/Sustain/Slide)"),
        ("observatory-TRACE", "LOOP OBSERVATORY - 4 voices, aligned"),
        ("focus", "FOCUS - the existing context page, framed"),
    ]
    gap = 24
    header = 60
    sheet = Image.new("RGB", (len(panels) * WIDTH + (len(panels) + 1) * gap,
                              HEIGHT + header + gap), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    for index, (name, label) in enumerate(panels):
        image = images.get(name)
        if image is None:
            continue
        x = gap + index * (WIDTH + gap)
        sheet.paste(image, (x, header))
        caption(draw, (x, 22), label, 17, ACCENT)
    sheet.save(OUT / "pages-overview.png")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", help="host C++ compiler to configure the test build with")
    parser.add_argument("--skip-build", action="store_true",
                        help="reuse the existing pico2seq_display_demo binary")
    args = parser.parse_args()

    if not args.skip_build:
        build_demo(args.cxx)
    run_demo()
    images = convert()
    style_comparison(images)
    layout_map(images)
    pages_overview(images)
    print(f"wrote {len(images)} screen images and 3 sheets into {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
