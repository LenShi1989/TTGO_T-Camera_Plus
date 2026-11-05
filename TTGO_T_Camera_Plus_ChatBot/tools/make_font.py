#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_font.py - build the 16x16 Traditional Chinese bitmap font for the
TTGO T-Camera Plus ChatBot TFT screen.

Source font : GNU Unifont (OFL-1.1 / GPLv2+ with font embedding exception)
Output      : data/font_tc16.bin   (copied into SPIFFS by the IDE plugin)

Binary format (little endian):

    offset 0   char[4]  magic = "TCF2"
    offset 4   uint16   max_width  (16)
    offset 6   uint16   max_height (16)
    offset 8   uint32   count
    offset 12  uint32   stride     (38 bytes per entry)
    offset 16  entries[count], sorted by codepoint:
        uint32 codepoint
        uint8  width   (8 or 16)
        uint8  height  (16)
        uint8  bitmap[32]   row-major, MSB first
                            width  8 -> 1 byte per row, first 16 bytes used
                            width 16 -> 2 bytes per row, all 32 bytes used

The firmware loads the whole file into PSRAM and does a binary search per
character, so no RAM is wasted on an in-memory index.

Character set: printable ASCII + every decodable Big5 character + CJK
punctuation + full width forms.  Big5 is what Traditional Chinese needs.

Usage:
    python tools/make_font.py
    python tools/make_font.py --font C:\\path\\to\\unifont.otf
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "data", "font_tc16.bin")
CACHE = os.path.join(HERE, ".cache")
UNIFONT_URL = (
    "https://unifoundry.com/pub/unifont/unifont-18.0.01/"
    "font-builds/unifont-18.0.01.otf"
)
UNIFONT_NAME = "unifont-18.0.01.otf"

# Symbols that are handy on a chat screen but are not part of Big5.
EXTRA_CHARS = (
    "°±×÷≠≤≥→←↑↓★☆♥✓✗•…—‘’“”「」『』〈〉《》【】"
    "　、。〃々〆〇〔〕〖〗〘〙〚〛〜〝〞"
    "！？；：，．（）［］｛｝＜＞＝＋－＊／％＃＠＆"
)

HEADER = struct.Struct("<4sHHII")
ENTRY_STRIDE = 38


def download_font() -> str:
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, UNIFONT_NAME)
    if os.path.exists(path) and os.path.getsize(path) > 1_000_000:
        return path
    print("Downloading GNU Unifont to", path)
    urllib.request.urlretrieve(UNIFONT_URL, path)
    print("Downloaded", os.path.getsize(path), "bytes")
    return path


def build_charset() -> list[str]:
    chars: set[str] = set()

    for cp in range(0x20, 0x7F):  # printable ASCII
        chars.add(chr(cp))

    # every valid Big5 two byte sequence (Traditional Chinese + symbols)
    for hi in range(0x81, 0xFF):
        for lo in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            try:
                ch = bytes((hi, lo)).decode("big5")
            except Exception:
                continue
            if len(ch) == 1 and not ch.isspace():
                chars.add(ch)

    # CJK punctuation and full width forms
    for cp in range(0x3000, 0x3040):
        chars.add(chr(cp))
    for cp in range(0xFF01, 0xFF5F):
        chars.add(chr(cp))
    chars.update(EXTRA_CHARS)

    return sorted(chars, key=ord)


def glyph_width(font: ImageFont.FreeTypeFont, ch: str, mask_w: int) -> int:
    """Unifont is dual width: 8 px for Latin, 16 px for CJK / full width."""
    try:
        advance = float(font.getlength(ch))
    except Exception:
        advance = 0.0
    return 16 if (advance > 8.5 or mask_w > 8) else 8


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, width: int) -> bytes:
    img = Image.new("1", (16, 16), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), ch, font=font, fill=1)
    px = img.load()
    out = bytearray(32)
    row_bytes = width // 8
    for y in range(16):
        for x in range(width):
            if px[x, y]:
                out[y * row_bytes + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--font", help="path to a 16 px monochrome TTF/OTF file")
    ap.add_argument("--out", default=OUT, help="output .bin path")
    args = ap.parse_args()

    font_path = args.font or download_font()
    if not os.path.exists(font_path):
        print("Font not found:", font_path, file=sys.stderr)
        return 1

    font = ImageFont.truetype(font_path, 16)
    charset = build_charset()

    entries: list[tuple[int, int, bytes]] = []
    missing: list[str] = []

    for ch in charset:
        try:
            mask = font.getmask(ch, mode="1")
        except Exception:
            missing.append(ch)
            continue
        if mask.size[0] == 0 and mask.size[1] == 0:
            missing.append(ch)
            continue
        width = glyph_width(font, ch, mask.size[0])
        bitmap = render_glyph(font, ch, width)
        if not any(bitmap):
            if ch not in (" ", "\u3000"):
                missing.append(ch)
                continue
        entries.append((ord(ch), width, bitmap))

    entries.sort(key=lambda item: item[0])

    buf = bytearray(HEADER.pack(b"TCF2", 16, 16, len(entries), ENTRY_STRIDE))
    for cp, width, bitmap in entries:
        buf += struct.pack("<IBB", cp, width, 16)
        buf += bitmap
    assert len(buf) == HEADER.size + len(entries) * ENTRY_STRIDE

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(buf)

    wide = sum(1 for _, w, _ in entries if w == 16)
    print(f"font    : {font_path}")
    print(f"glyphs  : {len(entries)} ({wide} wide, {len(entries) - wide} narrow)")
    print(f"missing : {len(missing)} {ascii(''.join(missing[:30]))}")
    print(f"output  : {args.out}")
    print(f"size    : {len(buf):,} bytes ({len(buf) / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
