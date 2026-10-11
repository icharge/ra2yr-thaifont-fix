#!/usr/bin/env python3
"""Render the supplied RA2 FNT glyphs using the game and ThaiFontFix layout rules."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent.parent
FNT_PATH = ROOT / "game.fnt"
OUT_PATH = Path(__file__).resolve().parent / "thai-render-test.png"
FNT_HEADER_SIZE = 28
MAP_SIZE = 0x10000 * 2
ROW_BYTES = 3
GLYPH_HEIGHT = 16

THAI_MARKS = {0xD1, *range(0xD4, 0xDB), *range(0xE7, 0xEF)}
THAI_UPPER_MARKS = {0xD1, *range(0xD4, 0xD8), *range(0xE7, 0xEF)}
THAI_TONE_MARKS = set(range(0xE8, 0xEC))


@dataclass(frozen=True)
class Glyph:
    code: int
    index: int
    width: int
    rows: tuple[bytes, ...]


class GameFont:
    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        if data[:4] != b"fonT":
            raise ValueError(f"{path} is not an RA2 fonT file")
        self.count = struct.unpack_from("<I", data, 20)[0]
        self.stride = struct.unpack_from("<I", data, 24)[0]
        self.map_offset = FNT_HEADER_SIZE
        self.records_offset = self.map_offset + MAP_SIZE
        expected_size = self.records_offset + self.count * self.stride
        if self.stride < 1 + GLYPH_HEIGHT * ROW_BYTES or len(data) != expected_size:
            raise ValueError(
                f"Unexpected FNT dimensions: count={self.count}, stride={self.stride}, "
                f"file-size={len(data)}, expected={expected_size}"
            )
        self.data = data
        self.safe_spacing = 8
        for index in range(self.count):
            pos = self.records_offset + index * self.stride
            width = self.data[pos]
            if width > 16:
                raise ValueError(f"Glyph record {index} has invalid width {width}")
            for row_index in range(GLYPH_HEIGHT):
                row = self.data[pos + 1 + row_index * ROW_BYTES : pos + 1 + (row_index + 1) * ROW_BYTES]
                for x in range(width, width + 8):
                    if row[x >> 3] & (0x80 >> (x & 7)):
                        self.safe_spacing = min(self.safe_spacing, x - width)

    def glyph(self, code: int) -> Glyph:
        if not 0 <= code <= 0xFFFF:
            raise ValueError(f"Not a 16-bit game character code: {code}")
        index_plus_one = struct.unpack_from("<H", self.data, self.map_offset + code * 2)[0]
        if not 1 <= index_plus_one <= self.count:
            raise ValueError(f"Game FNT has no glyph for CP874 byte 0x{code:02X}")
        index = index_plus_one - 1
        pos = self.records_offset + index * self.stride
        width = self.data[pos]
        rows = tuple(
            self.data[pos + 1 + row * ROW_BYTES : pos + 1 + (row + 1) * ROW_BYTES]
            for row in range(GLYPH_HEIGHT)
        )
        return Glyph(code, index, width, rows)


def encode_cp874(text: str) -> list[int]:
    """Encode the Thai/ASCII subset used by these samples without host code-page dependencies."""
    result = []
    for char in text:
        value = ord(char)
        if value < 0x80:
            result.append(value)
        elif 0x0E01 <= value <= 0x0E3A:
            result.append(value - 0x0D60)
        elif value == 0x0E3F:
            result.append(0xA0)
        elif 0x0E40 <= value <= 0x0E5B:
            result.append(value - 0x0D60)
        else:
            raise ValueError(f"Sample character U+{value:04X} is outside the supported CP874 range")
    return result


def bit_is_set(glyph: Glyph, x: int, y: int) -> bool:
    row = glyph.rows[y]
    return bool(row[x >> 3] & (0x80 >> (x & 7)))


def sara_am_top_pixels(glyph: Glyph) -> set[tuple[int, int]] | None:
    """Find the compact upper nikhahit above Sara Aa's right-side stroke."""
    if glyph.code != 0xD3 or glyph.width < 2:
        return None
    vertical_run_start = None
    vertical_run_length = 0
    run_start = 0
    run_length = 0
    for y in range(GLYPH_HEIGHT + 1):
        ink = y < GLYPH_HEIGHT and bit_is_set(glyph, glyph.width - 1, y)
        if ink:
            if run_length == 0:
                run_start = y
            run_length += 1
        else:
            if run_length > vertical_run_length:
                vertical_run_start = run_start
                vertical_run_length = run_length
            run_length = 0
    if vertical_run_start is None or vertical_run_length < 3 or vertical_run_start < 2:
        return None
    split_row = vertical_run_start - 1
    top_pixels = {
        (x, y)
        for y in range(split_row)
        for x in range(glyph.width)
        if bit_is_set(glyph, x, y)
    }
    lower_pixels = {
        (x, y)
        for y in range(split_row, GLYPH_HEIGHT)
        for x in range(glyph.width)
        if bit_is_set(glyph, x, y)
    }
    if len(top_pixels) < 2 or len(lower_pixels) < 3 or len(top_pixels) >= len(lower_pixels):
        return None
    if max(x for x, _ in top_pixels) >= glyph.width - 1:
        return None
    return top_pixels


def draw_glyph(
    draw: ImageDraw.ImageDraw,
    glyph: Glyph,
    x: int,
    y: int,
    draw_width: int,
    scale: int,
    color: tuple[int, int, int],
    pixel_mask: set[tuple[int, int]] | None = None,
) -> None:
    # This matches the bitmap bit order and fixed 3-byte row layout used by the shim.
    for gy in range(GLYPH_HEIGHT):
        for gx in range(min(draw_width, 24)):
            if pixel_mask is not None and (gx, gy) not in pixel_mask:
                continue
            if bit_is_set(glyph, gx, gy):
                x0 = (x + gx) * scale
                y0 = (y + gy) * scale
                draw.rectangle((x0, y0, x0 + scale - 1, y0 + scale - 1), fill=color)


def draw_sequence(
    draw: ImageDraw.ImageDraw,
    font: GameFont,
    codes: list[int],
    x: int,
    y: int,
    scale: int,
    spacing: int,
    patched: bool,
    tone_raise: int,
    color: tuple[int, int, int],
) -> int:
    cursor = x
    base_x = 0
    base_width = 0
    has_base = False
    has_above_mark = False
    glyphs = [font.glyph(code) for code in codes]

    for glyph_index, glyph in enumerate(glyphs):
        code = glyph.code
        is_mark = code in THAI_MARKS
        previous_is_tone = glyph_index > 0 and glyphs[glyph_index - 1].code in THAI_TONE_MARKS
        next_is_sara_am = glyph_index + 1 < len(glyphs) and glyphs[glyph_index + 1].code == 0xD3
        if patched and code == 0xD3 and has_base:
            top_pixels = sara_am_top_pixels(glyph)
            if top_pixels:
                lower_pixels = {
                    (gx, gy)
                    for gy in range(GLYPH_HEIGHT)
                    for gx in range(glyph.width)
                    if bit_is_set(glyph, gx, gy) and (gx, gy) not in top_pixels
                }
                min_x = min(gx for gx, _ in top_pixels)
                max_x = max(gx for gx, _ in top_pixels)
                mark_width = max_x - min_x + 1
                mark_x = base_x + (base_width - mark_width) // 2 - min_x
                mark_y = y - tone_raise if has_above_mark and not previous_is_tone else y
                draw_glyph(
                    draw, glyph, mark_x, mark_y, glyph.width + spacing, scale, color,
                    pixel_mask=top_pixels,
                )
                draw_glyph(
                    draw, glyph, cursor, y, glyph.width + spacing, scale, color,
                    pixel_mask=lower_pixels,
                )
                has_above_mark = True
                cursor += glyph.width + spacing
                continue
        if patched and is_mark:
            draw_x = base_x + (base_width - glyph.width) // 2 if has_base else cursor
            draw_y = (
                y - tone_raise
                if code in THAI_TONE_MARKS and (has_above_mark or next_is_sara_am)
                else y
            )
            draw_glyph(draw, glyph, draw_x, draw_y, glyph.width, scale, color)
            if code in THAI_UPPER_MARKS:
                has_above_mark = True
            # The hook returns the incoming x for marks: they take no layout advance.
            continue

        if patched:
            # ApplyMetrics stores width+spacing in regular glyph records and sets font spacing to 0.
            draw_width = glyph.width + spacing
            advance = draw_width
        else:
            # The game advances by the bitmap width plus its font spacing.
            draw_width = glyph.width
            advance = glyph.width + spacing

        draw_glyph(draw, glyph, cursor, y, draw_width, scale, color)
        if patched:
            base_x = cursor
            base_width = glyph.width
            has_base = True
            has_above_mark = False
        else:
            has_base = True
            base_x = cursor
            base_width = glyph.width
            has_above_mark = False
        cursor += advance

    return cursor


def load_label_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        Path("C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf"),
        Path("C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spacing", type=int, default=2, help="game font spacing to simulate (default: 2)")
    parser.add_argument("--tone-raise", type=int, default=2, help="ThaiFontFix tone mark raise in pixels")
    parser.add_argument("--scale", type=int, default=4, help="integer enlargement for bitmap glyphs")
    parser.add_argument("--output", type=Path, default=OUT_PATH, help="output PNG path")
    args = parser.parse_args()
    if not 0 <= args.spacing <= 8 or not 1 <= args.scale <= 12 or not 0 <= args.tone_raise <= 16:
        parser.error("spacing must be 0-8, scale 1-12, and tone-raise 0-16")

    font = GameFont(FNT_PATH)
    if args.spacing > font.safe_spacing:
        parser.error(
            f"the ThaiFontFix safety check would reject spacing={args.spacing}; "
            f"this FNT supports at most {font.safe_spacing}px"
        )
    samples = [
        ("Upper vowel + tone mark", "กี่"),
        ("Leading vowel + tone mark", "เก่ง"),
        ("Sara Am", "ดำ"),
        ("Tone + Sara Am", "ต่ำ"),
        ("Thai text control sample", "ภาษาไทย"),
    ]
    panel_w, panel_h = 590, 174
    margin, title_h = 24, 78
    image = Image.new("RGB", (margin * 2 + panel_w * 2, title_h + panel_h * len(samples) + margin), (14, 18, 35))
    draw = ImageDraw.Draw(image)
    title_font = load_label_font(23, bold=True)
    subtitle_font = load_label_font(14)
    panel_title_font = load_label_font(16, bold=True)
    small_font = load_label_font(12)
    draw.text((margin, 13), "RA2 game.fnt: current layout vs Thai mark adjustment", font=title_font, fill=(245, 241, 205))
    draw.text((margin, 47), f"Bitmap pixels from game.fnt · reconstructed layout · spacing = {args.spacing}px (safe max {font.safe_spacing}) · glyphs enlarged {args.scale}x", font=subtitle_font, fill=(168, 187, 213))

    scale = args.scale
    for sample_idx, (description, text) in enumerate(samples):
        y0 = title_h + sample_idx * panel_h
        codes = encode_cp874(text)
        # Validate that every byte is mapped before drawing either pass.
        for code in codes:
            font.glyph(code)
        code_text = "CP874: " + " ".join(f"{code:02X}" for code in codes)
        for col, patched in enumerate((False, True)):
            x0 = margin + col * panel_w
            panel = (x0, y0, x0 + panel_w - 12, y0 + panel_h - 12)
            draw.rounded_rectangle(panel, radius=8, fill=(24, 31, 54), outline=(63, 79, 112), width=1)
            label = "GAME LAYOUT" if not patched else "THAIFNT ADJUSTMENT"
            draw.text((x0 + 16, y0 + 12), label, font=panel_title_font, fill=(116, 213, 194) if patched else (250, 193, 111))
            draw.text((x0 + 16, y0 + 36), description, font=small_font, fill=(195, 204, 221))
            draw.text((x0 + 16, y0 + 54), code_text, font=small_font, fill=(145, 161, 187))
            glyph_y = y0 + 79
            # A faint row guide makes vertical stacking/overlap visible.
            baseline_y = glyph_y + GLYPH_HEIGHT * scale
            draw.line((x0 + 16, baseline_y, x0 + panel_w - 34, baseline_y), fill=(49, 62, 88), width=1)
            draw_sequence(
                draw,
                font,
                codes,
                (x0 + 24) // scale,
                glyph_y // scale,
                scale,
                args.spacing,
                patched,
                args.tone_raise,
                (255, 232, 87),
            )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f"Rendered {len(samples)} samples from {FNT_PATH}")
    print(f"FNT: {font.count} glyph records, stride {font.stride}, safe spacing {font.safe_spacing}px; CP874 glyph maps verified")
    print(f"Simulated spacing: {args.spacing}px; tone raise: {args.tone_raise}px")
    print(f"PNG: {args.output.resolve()}")


if __name__ == "__main__":
    main()
