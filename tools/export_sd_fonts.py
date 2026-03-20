#!/usr/bin/env python3
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FONT_SOURCES = [
    (ROOT / "src/fonts/FreeSans7pt7b.h", ROOT / "sdcard/fonts/free_sans_7.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSans9pt7b.h", ROOT / "sdcard/fonts/free_sans_9.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSans12pt7b.h", ROOT / "sdcard/fonts/free_sans_12.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSans18pt7b.h", ROOT / "sdcard/fonts/free_sans_18.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSans24pt7b.h", ROOT / "sdcard/fonts/free_sans_24.gfxf"),
    (ROOT / "src/fonts/FreeSans32pt7b.h", ROOT / "sdcard/fonts/free_sans_32.gfxf"),
    (ROOT / "src/fonts/FreeSansBold7pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_7.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSansBold9pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_9.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSansBold12pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_12.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSansBold18pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_18.gfxf"),
    (ROOT / ".pio/libdeps/ttgo-t5-213/Adafruit GFX Library/Fonts/FreeSansBold24pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_24.gfxf"),
    (ROOT / "src/fonts/FreeSansBold32pt7b.h", ROOT / "sdcard/fonts/free_sans_bold_32.gfxf"),
    (ROOT / "src/fonts/FreeMono7pt7b.h", ROOT / "sdcard/fonts/free_mono_7.gfxf"),
]


def parse_int_tokens(block: str):
    # Preserve signs for decimal literals (e.g. -1 in glyph x/y offsets).
    # Hex literals in these headers are unsigned and remain as-is.
    return [int(tok, 0) for tok in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", block)]


def parse_font_header(path: Path):
    text = path.read_text(encoding="utf-8")

    m_bitmap = re.search(r"Bitmaps\[\]\s+PROGMEM\s*=\s*\{(.*?)\};", text, re.S)
    if not m_bitmap:
        raise ValueError(f"Could not parse bitmaps from {path}")
    bitmap_bytes = bytes(parse_int_tokens(m_bitmap.group(1)))

    m_glyph = re.search(r"Glyphs\[\]\s+PROGMEM\s*=\s*\{(.*?)\};", text, re.S)
    if not m_glyph:
        raise ValueError(f"Could not parse glyphs from {path}")

    glyphs = []
    for row in re.findall(r"\{([^\}]*)\}", m_glyph.group(1)):
        vals = parse_int_tokens(row)
        if len(vals) != 6:
            continue
        off, w, h, xa, xo, yo = vals
        if xo >= 128:
            xo -= 256
        if yo >= 128:
            yo -= 256
        glyphs.append((off, w, h, xa, xo, yo))

    m_font = re.search(r"0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*(\d+)\s*\}\s*;", text)
    if not m_font:
        raise ValueError(f"Could not parse font metadata from {path}")

    first = int(m_font.group(1), 16)
    last = int(m_font.group(2), 16)
    y_advance = int(m_font.group(3))

    if len(glyphs) != (last - first + 1):
        raise ValueError(
            f"Glyph count mismatch in {path}: parsed {len(glyphs)} expected {last - first + 1}"
        )

    return first, last, y_advance, glyphs, bitmap_bytes


def write_gfxf(dest: Path, first: int, last: int, y_advance: int, glyphs, bitmap_bytes: bytes):
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as f:
        # Header: magic(4) + version(1) + first(1) + last(1) + yAdvance(1) + glyphCount(2) + bitmapSize(4)
        f.write(b"GFXF")
        f.write(struct.pack("<BBBB", 1, first, last, y_advance))
        f.write(struct.pack("<H", len(glyphs)))
        f.write(struct.pack("<I", len(bitmap_bytes)))

        # Glyph table: bitmapOffset(4) + width(1) + height(1) + xAdvance(1) + xOffset(1) + yOffset(1)
        for off, w, h, xa, xo, yo in glyphs:
            f.write(struct.pack("<IBBBbb", off, w, h, xa, xo, yo))

        # Raw packed bitmap bytes.
        f.write(bitmap_bytes)


if __name__ == "__main__":
    failures = []
    for src, out in FONT_SOURCES:
        if not src.exists():
            failures.append(f"Missing source header: {src}")
            continue
        try:
            first, last, y_advance, glyphs, bitmap_bytes = parse_font_header(src)
            write_gfxf(out, first, last, y_advance, glyphs, bitmap_bytes)
            print(f"Exported {out.relative_to(ROOT)} ({len(bitmap_bytes)} bitmap bytes)")
        except Exception as exc:
            failures.append(f"{src}: {exc}")

    if failures:
        print("\nErrors:")
        for item in failures:
            print(f"- {item}")
        raise SystemExit(1)

    print("\nDone.")
