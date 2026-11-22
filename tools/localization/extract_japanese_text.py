#!/usr/bin/env python3
"""Extract Wildroid 9's Japanese text into generated/wild9_japanese_text.c.

Reads the player's Wildroid 9 (Japan, SCPS-10080) disc and writes a C source
holding what the USA build needs to show Japanese:

  * the two Japanese text packages from W9.CDD (entry 0x82, and 0x83 used
    on level 4): the in-game script with its Shift-JIS strings;
  * the 152 glyphs of fonts 0x6A/0x6B (W9.CDD entry 0x1D), decoded to
    15-bit texels in one 256x160 atlas, with their metrics.

The output goes into generated/, which git ignores and which
scripts/pack_generated_bundle.py ships alongside the recompiled C, so release
builds need only the USA disc. Never commit the output.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import NoReturn, cast

# Run as a script, never imported as a package: psx_disc is a sibling module.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from psx_disc import (  # pyright: ignore[reportImplicitRelativeImport]
    SECTOR_BYTES,
    USER_BYTES,
    DataTrack,
    directory_entries,
    parse_cue,
)

# Shift-JIS code, glyph box (x0, y0, x1, y1), atlas cell (u, v) and size (w, h).
Glyph = tuple[int, int, int, int, int, int, int, int, int]

JP_DATA_TRACK_SIZE = 84_711_424
JP_DATA_TRACK_SHA256 = (
    "eea7bbe08ac2f650b3e336cd536c00ee78aaa1a07f5b900743aade4853549e09"
)

# Zero-based W9.IDX chunks (W9.CDD entries are one-based in the executable).
CHUNK_TEXT_MAIN = 0x81
CHUNK_TEXT_LEVEL4 = 0x82
CHUNK_FONTS = 0x1C

FONT_W, FONT_H, CELL = 256, 160, 16

# Shift-JIS code of each glyph, in FR2D order (record n is glyph n + 1),
# from SCPS-10080 FUN_80025A38.
CODES = {
    0x6B: [
        0x8141,
        0x8142,
        0x8143,
        0x8145,
        0x8148,
        0x8149,
        0x815B,
        0x8163,
        0x8167,
        0x8168,
        0x8250,
        0x8251,
        0x8252,
        0x8258,
        0x8264,
        0x82A0,
        0x82A2,
        0x82A4,
        0x82A6,
        0x82A8,
        0x82A9,
        0x82AA,
        0x82AB,
        0x82AC,
        0x82AD,
        0x82AE,
        0x82AF,
        0x82B0,
        0x82B1,
        0x82B2,
        0x82B3,
        0x82B5,
        0x82B6,
        0x82B7,
        0x82B8,
        0x82B9,
        0x82BA,
        0x82BB,
        0x82BC,
        0x82BD,
        0x82BE,
        0x82BF,
        0x82C1,
        0x82C2,
        0x82C3,
        0x82C4,
        0x82C5,
        0x82C6,
        0x82C7,
        0x82C8,
        0x82C9,
        0x82CB,
        0x82CC,
        0x82CD,
        0x82CE,
        0x82D0,
        0x82D1,
        0x82D3,
        0x82D4,
        0x82D6,
        0x82D7,
        0x82DA,
        0x82DC,
        0x82DD,
        0x82DE,
        0x82DF,
        0x82E0,
        0x82E1,
        0x82E2,
        0x82E3,
        0x82E5,
        0x82E6,
        0x82E7,
        0x82E8,
        0x82E9,
        0x82EA,
        0x82EB,
        0x82ED,
        0x82F0,
        0x82F1,
    ],
    0x6A: [
        0x8341,
        0x8342,
        0x8343,
        0x8345,
        0x8346,
        0x8347,
        0x8349,
        0x834A,
        0x834B,
        0x834C,
        0x834E,
        0x834F,
        0x8350,
        0x8351,
        0x8352,
        0x8354,
        0x8356,
        0x8357,
        0x8358,
        0x8359,
        0x835A,
        0x835E,
        0x835F,
        0x8360,
        0x8362,
        0x8363,
        0x8365,
        0x8366,
        0x8367,
        0x8368,
        0x8369,
        0x836A,
        0x836C,
        0x836D,
        0x836E,
        0x836F,
        0x8370,
        0x8372,
        0x8373,
        0x8374,
        0x8375,
        0x8376,
        0x8377,
        0x8378,
        0x8379,
        0x837A,
        0x837B,
        0x837C,
        0x837D,
        0x837E,
        0x8380,
        0x8382,
        0x8383,
        0x8384,
        0x8385,
        0x8387,
        0x8389,
        0x838A,
        0x838B,
        0x838C,
        0x838D,
        0x838F,
        0x8393,
        0x8945,
        0x89CE,
        0x8CF5,
        0x8DB6,
        0x90AF,
        0x90B3,
        0x90DA,
        0x91B1,
        0x924A,
    ],
}


def fail(message: str) -> NoReturn:
    raise SystemExit(f"extract_japanese_text: {message}")


def read_data_track(cue: Path) -> tuple[bytes, DataTrack]:
    tracks = parse_cue(cue)
    if not tracks:
        fail(f"no data track in {cue}")
    raw = tracks[0].path.read_bytes()
    if len(raw) % SECTOR_BYTES:
        fail(f"{tracks[0].path} is not a MODE2/2352 image")
    out = bytearray()
    for pos in range(0, len(raw), SECTOR_BYTES):
        sector = raw[pos : pos + SECTOR_BYTES]
        start = {1: 16, 2: 24}.get(sector[15])
        if start is None:
            fail(f"unsupported sector mode {sector[15]} at LBA {pos // SECTOR_BYTES}")
        out += sector[start : start + USER_BYTES]
    if (
        len(out) != JP_DATA_TRACK_SIZE
        or hashlib.sha256(out).hexdigest() != JP_DATA_TRACK_SHA256
    ):
        fail("this is not the supported Wildroid 9 (Japan) SCPS-10080 disc")
    return bytes(out), tracks[0]


def iso_file(
    track_bytes: bytes, entries: dict[str, tuple[int, int, int]], name: str
) -> bytes:
    if name not in entries:
        fail(f"{name} missing from the disc")
    extent, size, _ = entries[name]
    return track_bytes[extent * USER_BYTES : extent * USER_BYTES + size]


def chunk(idx: bytes, cdd: bytes, index: int) -> bytes:
    count = struct.unpack_from("<I", idx, 0)[0]
    if index >= count:
        fail(f"W9.IDX has no chunk {index:#x}")
    begin, end = struct.unpack_from("<II", idx, 4 + index * 4)
    return cdd[begin:end]


def records(data: bytes) -> Iterator[tuple[int, str, int]]:
    pos = 0
    while pos + 12 <= len(data):
        tag = data[pos : pos + 4][::-1].decode("latin-1")
        size = struct.unpack_from("<I", data, pos + 8)[0]
        yield pos, tag, size
        if tag == "EXIT" or size < 12:
            return
        pos += size


def text_package(data: bytes) -> bytes:
    tags = list(records(data))
    if [t for _, t, _ in tags] != ["ENT2", "VC_A", "VCIB", "EXIT"]:
        fail("unexpected text package layout")
    end = tags[-1][0] + 12
    if end > 0x7EE4:
        fail(f"text package of {end:#x} bytes is larger than the USA slot")
    return data[:end]


def texel_reader(data: bytes, image: int, width: int) -> Callable[[int, int], int]:
    def texel(x: int, y: int) -> int:
        value: int = struct.unpack_from("<H", data, image + (y * width + x) * 2)[0]
        return value

    return texel


def decode_fonts(data: bytes) -> tuple[list[int], list[Glyph]]:
    pixels = [0] * (FONT_W * FONT_H)
    glyphs: list[Glyph] = []
    cell = 0
    vdat: tuple[int, int, int] | None = None
    found: dict[int, tuple[tuple[int, int, int], int]] = {}
    for pos, tag, _ in records(data):
        if tag == "VDAT":
            w, _, h = struct.unpack_from("<3H", data, pos + 12)
            vdat = (w, h, pos + 20)
        elif tag == "FR2D" and vdat:
            font = struct.unpack_from("<i", data, pos + 12)[0]
            if font in CODES and font not in found:
                found[font] = (vdat, pos + 12)
    if set(found) != set(CODES):
        fail("fonts 0x6A/0x6B not found")
    for font in (0x6B, 0x6A):
        (w, _, image), base = found[font]
        texel = texel_reader(data, image, w)
        count = struct.unpack_from("<i", data, base + 4)[0]
        if count != len(CODES[font]) + 1:
            fail(f"font {font:#x} has {count} records")
        for n in range(1, count):
            r = base + 0x14 + n * 0x1C
            u0, v0, _, u1, v1, _, x0, y0, x1, y1, mode, tpx, tpy, cx, cy = (
                struct.unpack_from("<BBHBBHhhhhIHHHH", data, r)
            )
            per = {0: 4, 1: 2}.get(mode & 3, 1)
            gw, gh = u1 - u0, v1 - v0
            if not (0 < gw <= CELL and 0 < gh <= CELL):
                fail(f"font {font:#x} glyph {n} is {gw}x{gh}")
            bx, by = (cell % (FONT_W // CELL)) * CELL, (cell // (FONT_W // CELL)) * CELL
            for v in range(v0, v1):
                for u in range(u0, u1):
                    t = texel((tpx & ~63) + u // per, v + (tpy & 0x100))
                    if per == 4:
                        color = texel(cx + ((t >> ((u & 3) * 4)) & 15), cy)
                    elif per == 2:
                        color = texel(cx + ((t >> ((u & 1) * 8)) & 255), cy)
                    else:
                        color = t
                    pixels[(by + v - v0) * FONT_W + bx + u - u0] = color
            glyphs.append((CODES[font][n - 1], x0, y0, x1, y1, bx, by, gw, gh))
            cell += 1
    glyphs.sort()
    return pixels, glyphs


def c_bytes(name: str, data: bytes) -> str:
    rows = [
        ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        for i in range(0, len(data), 16)
    ]
    return (
        f"const uint8_t {name}[{len(data)}] = {{\n  "
        + ",\n  ".join(rows)
        + f"\n}};\nconst uint32_t {name}_size = {len(data)}u;\n"
    )


def write_c(
    path: Path, main: bytes, level4: bytes, pixels: list[int], glyphs: list[Glyph]
) -> None:
    rows = [
        ", ".join(f"0x{p:04x}" for p in pixels[i : i + 16])
        for i in range(0, len(pixels), 16)
    ]
    glyph_rows = [
        f"  {{0x{c:04x}, {x0}, {y0}, {x1}, {y1}, {u}, {v}, {w}, {h}}}"
        for c, x0, y0, x1, y1, u, v, w, h in glyphs
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    _ = path.write_text(
        "/* Generated by tools/localization/extract_japanese_text.py from Wildroid 9\n"
        + " * (SCPS-10080). Game data: ships in the generated/ bundle, never in git. */\n"
        + '#include "wild9_japanese_text_data.h"\n\n'
        + c_bytes("wild9_ja_text_main", main)
        + "\n"
        + c_bytes("wild9_ja_text_level4", level4)
        + "\n"
        + "const uint16_t wild9_ja_font_pixels[WILD9_JA_FONT_W * WILD9_JA_FONT_H] = {\n  "
        + ",\n  ".join(rows)
        + "\n};\n\n"
        + "const Wild9JaGlyph wild9_ja_glyphs[] = {\n"
        + ",\n".join(glyph_rows)
        + "\n};\n"
        + f"const uint32_t wild9_ja_glyph_count = {len(glyphs)}u;\n",
        encoding="ascii",
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0] if __doc__ else None
    )
    _ = parser.add_argument(
        "--disc", type=Path, required=True, help="Wildroid 9 (Japan) .cue"
    )
    root = Path(__file__).resolve().parents[2]
    _ = parser.add_argument(
        "--output", type=Path, default=root / "generated" / "wild9_japanese_text.c"
    )
    args = parser.parse_args()
    disc = cast(Path, args.disc)
    output = cast(Path, args.output)

    track_bytes, track = read_data_track(disc)
    entries = directory_entries(track)
    idx = iso_file(track_bytes, entries, "/W9.IDX")
    cdd = iso_file(track_bytes, entries, "/W9.CDD")
    main_pkg = text_package(chunk(idx, cdd, CHUNK_TEXT_MAIN))
    level4_pkg = text_package(chunk(idx, cdd, CHUNK_TEXT_LEVEL4))
    pixels, glyphs = decode_fonts(chunk(idx, cdd, CHUNK_FONTS))
    write_c(output, main_pkg, level4_pkg, pixels, glyphs)
    print(
        f"Wrote {output}: text {len(main_pkg)}+{len(level4_pkg)} bytes, "
        + f"{len(glyphs)} glyphs"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
