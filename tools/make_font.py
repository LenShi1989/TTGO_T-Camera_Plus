#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_font.py - 從 GNU Unifont .hex 產生 TTGO AI Camera 用的 16x16 點陣字型檔。

用法:
    python tools/make_font.py <unifont.hex> <sd_card/font.bin>

字型檔格式 (little-endian):
    header:
        char     magic[4]      = "TGF1"
        uint16_t version       = 1
        uint16_t width         = 16
        uint16_t height        = 16
        uint16_t flags         = 0
        uint32_t count
        uint32_t index_offset  = 24
        uint32_t data_offset   = 24 + count * 8
    index[count]:
        uint32_t codepoint
        uint32_t glyph_offset  (相對於 data_offset)
    data:
        每個字型 32 bytes = 16 列 x 2 bytes，MSB 在左。

Unifont 授權: GNU GPLv2+ with font embedding exception。
詳見 https://unifoundry.com/unifont/
"""
import gzip
import os
import struct
import sys

HEADER_SIZE = 24


def load_unifont(path):
    opener = gzip.open if path.endswith(".gz") else open
    glyphs = {}
    with opener(path, "rt", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or ":" not in line:
                continue
            code_s, hex_s = line.split(":", 1)
            try:
                cp = int(code_s, 16)
            except ValueError:
                continue
            raw = bytes.fromhex(hex_s)
            if len(raw) == 16:          # 8x16 -> 左對齊成 16x16
                out = bytearray()
                for b in raw:
                    out += bytes((b, 0))
                raw = bytes(out)
            elif len(raw) != 32:
                continue
            glyphs[cp] = raw
    return glyphs


def big5_codepoints():
    cps = set()
    for lead in range(0xA1, 0xFA):
        for trail in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            try:
                ch = bytes((lead, trail)).decode("big5")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                cps.add(ord(ch))
    return cps


def wanted_codepoints():
    cps = set()
    cps.update(range(0x20, 0x7F))          # ASCII
    cps.update(range(0xA0, 0x100))         # Latin-1
    cps.update(range(0x2000, 0x2070))      # 一般標點
    cps.update(range(0x2500, 0x2580))      # 框線
    cps.update(range(0x25A0, 0x25D0))      # 幾何圖形
    cps.update(range(0x3000, 0x3040))      # CJK 標點
    cps.update(range(0xF900, 0xFB00))      # 相容表意文字
    cps.update(range(0xFF00, 0xFFF0))      # 全形字
    cps.update((0x00B0, 0x00B1, 0x00B7, 0x00D7, 0x00F7,
                0x2103, 0x2109, 0x2190, 0x2191, 0x2192, 0x2193,
                0x2605, 0x2606, 0x2665, 0x2666))
    cps.update(big5_codepoints())
    return cps


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    glyphs = load_unifont(src)
    wanted = wanted_codepoints()
    entries = sorted((cp, glyphs[cp]) for cp in wanted if cp in glyphs)

    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    count = len(entries)
    data_offset = HEADER_SIZE + count * 8

    with open(dst, "wb") as f:
        f.write(b"TGF1")
        f.write(struct.pack("<HHHHIII", 1, 16, 16, 0, count, HEADER_SIZE, data_offset))
        offset = 0
        for cp, _ in entries:
            f.write(struct.pack("<II", cp, offset))
            offset += 32
        for _, glyph in entries:
            f.write(glyph)

    print("wrote %s: %d glyphs, %d bytes" % (dst, count, os.path.getsize(dst)))
    print("index in RAM: %d bytes" % (count * 8))
    return 0


if __name__ == "__main__":
    sys.exit(main())
