#!/usr/bin/env python3
"""Render a small game-style preview from an RA2 game.fnt bitmap font."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT))
from render_fnt_test import Glyph, draw_sequence, encode_cp874  # noqa: E402


class PreviewFont:
    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        if data[:4] != b"fonT":
            raise ValueError(f"{path} is not an RA2 fonT file")
        self.data = data
        self.count = struct.unpack_from("<I", data, 20)[0]
        self.stride = struct.unpack_from("<I", data, 24)[0]
        self.records_offset = 28 + 0x20000
        if self.stride < 49 or len(data) != self.records_offset + self.count * self.stride:
            raise ValueError(f"Unsupported FNT layout: {path}")

    def glyph(self, code: int) -> Glyph:
        index_plus_one = struct.unpack_from("<H", self.data, 28 + code * 2)[0]
        if not 1 <= index_plus_one <= self.count:
            raise ValueError(f"No glyph for CP874 byte 0x{code:02X}")
        position = self.records_offset + (index_plus_one - 1) * self.stride
        width = self.data[position]
        if width > 24:
            raise ValueError(f"Glyph 0x{code:02X} has unsupported width {width}")
        rows = tuple(self.data[position + 1 + y * 3 : position + 1 + (y + 1) * 3] for y in range(16))
        return Glyph(code, index_plus_one - 1, width, rows)


def render(font_path: Path, output_path: Path) -> None:
    font = PreviewFont(font_path)
    image = Image.new("RGB", (250, 104), (14, 18, 35))
    draw = ImageDraw.Draw(image)

    for text, y in (("Moving Out!, For my people !!!", 24), ("หมูมะนาว!, ผลไม้ปีโป้ !!!", 60)):
        draw_sequence(
            draw,
            font,
            encode_cp874(text),
            x=6,
            y=y,
            scale=1,
            spacing=1,
            patched=True,
            tone_raise=3,
            color=(255, 232, 87),
        )

    ImageDraw.Draw(image).rectangle((0, 0, 249, 103), outline=(70, 85, 118), width=1)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, format="BMP")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    render(args.font, args.output)


if __name__ == "__main__":
    main()
