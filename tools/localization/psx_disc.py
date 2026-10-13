#!/usr/bin/env python3
"""Read PSX MODE2/2352 cue/bin data tracks and inspect W9 archive chunks.

This tool emits metadata and hashes by default. Extracted game data is written
only to an explicitly supplied destination outside the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

SECTOR_BYTES = 2352
USER_BYTES = 2048
W9_LANG_CHUNKS = {
    "en": 0x7B,
    "fr": 0x7C,
    "de": 0x7D,
    "it": 0x7E,
    "es": 0x7F,
}


@dataclass(frozen=True)
class DataTrack:
    path: Path
    mode: str


def parse_cue(path: Path) -> list[DataTrack]:
    current_file: Path | None = None
    tracks: list[DataTrack] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        file_match = re.match(r'\s*FILE\s+"([^"]+)"\s+(\S+)', line, re.I)
        if file_match:
            current_file = path.parent / file_match.group(1)
            continue
        track_match = re.match(r"\s*TRACK\s+\d+\s+(\S+)", line, re.I)
        if track_match and current_file is not None:
            mode = track_match.group(1).upper()
            if mode != "AUDIO":
                tracks.append(DataTrack(current_file, mode))
    return tracks


def read_user_sector(track: DataTrack, lba: int) -> bytes:
    with track.path.open("rb") as image:
        image.seek(lba * SECTOR_BYTES)
        raw = image.read(SECTOR_BYTES)
    if len(raw) != SECTOR_BYTES:
        raise ValueError(f"short sector read: {track.path}, LBA {lba}")
    mode = raw[15]
    if mode == 1:
        return raw[16:16 + USER_BYTES]
    if mode == 2:
        return raw[24:24 + USER_BYTES]
    raise ValueError(f"unsupported sector mode {mode} at LBA {lba}")


def directory_entries(track: DataTrack) -> dict[str, tuple[int, int, int]]:
    pvd = read_user_sector(track, 16)
    if pvd[0] != 1 or pvd[1:6] != b"CD001":
        raise ValueError(f"ISO9660 primary volume descriptor missing: {track.path}")
    root = pvd[156:190]
    root_lba = struct.unpack_from("<I", root, 2)[0]
    root_size = struct.unpack_from("<I", root, 10)[0]
    entries: dict[str, tuple[int, int, int]] = {}

    def walk(lba: int, size: int, prefix: str) -> None:
        blocks = (size + USER_BYTES - 1) // USER_BYTES
        data = b"".join(read_user_sector(track, lba + i) for i in range(blocks))
        pos = 0
        while pos < size:
            record_len = data[pos]
            if record_len == 0:
                pos = ((pos // USER_BYTES) + 1) * USER_BYTES
                continue
            rec = data[pos:pos + record_len]
            extent = struct.unpack_from("<I", rec, 2)[0]
            length = struct.unpack_from("<I", rec, 10)[0]
            flags = rec[25]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            if name not in (b"\x00", b"\x01"):
                component = name.split(b";")[0].decode("ascii", errors="replace")
                key = f"{prefix}/{component}"
                entries[key] = (extent, length, flags)
                if flags & 0x02:
                    walk(extent, length, key)
            pos += record_len

    walk(root_lba, root_size, "")
    return entries


def extract_file(track: DataTrack, extent: int, size: int, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with track.path.open("rb") as image, destination.open("wb") as output:
        for sector in range((size + USER_BYTES - 1) // USER_BYTES):
            image.seek((extent + sector) * SECTOR_BYTES)
            raw = image.read(SECTOR_BYTES)
            if len(raw) != SECTOR_BYTES:
                raise ValueError(f"short sector while extracting LBA {extent + sector}")
            mode = raw[15]
            start = 16 if mode == 1 else 24 if mode == 2 else -1
            if start < 0:
                raise ValueError(f"unsupported sector mode {mode} at LBA {extent + sector}")
            output.write(raw[start:start + USER_BYTES])
        output.truncate(size)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def cmd_list(args: argparse.Namespace) -> None:
    for track in parse_cue(args.cue):
        print(json.dumps({"data_track": str(track.path), "mode": track.mode,
                          "bytes": track.path.stat().st_size,
                          "sha256": sha256(track.path)}))
        for name, (extent, size, flags) in sorted(directory_entries(track).items()):
            print(json.dumps({"path": name, "lba": extent, "bytes": size,
                              "directory": bool(flags & 2)}))


def cmd_extract(args: argparse.Namespace) -> None:
    member = args.member.upper()
    for track in parse_cue(args.cue):
        entries = directory_entries(track)
        match = next((value for key, value in entries.items()
                      if key.upper() == member), None)
        if match is None:
            continue
        extent, size, flags = match
        if flags & 2:
            raise ValueError("refusing to extract a directory")
        extract_file(track, extent, size, args.output)
        print(json.dumps({"member": member, "lba": extent, "bytes": size,
                          "output": str(args.output), "sha256": sha256(args.output)}))
        return
    raise FileNotFoundError(f"{member} not found in {args.cue}")


def cmd_audit_chunks(args: argparse.Namespace) -> None:
    idx_bytes = args.index.read_bytes()
    if len(idx_bytes) < 129 * 4:
        raise ValueError("W9.IDX is too short for the 128-entry offset table")
    offsets = struct.unpack_from("<129I", idx_bytes)
    cdd_size = args.cdd.stat().st_size
    if offsets[0] != 0x7F or offsets[128] != cdd_size:
        raise ValueError("unexpected W9.IDX format or W9.CDD extent")
    with args.cdd.open("rb") as cdd:
        for language, chunk in W9_LANG_CHUNKS.items():
            start, end = offsets[chunk], offsets[chunk + 1]
            if start > end or end > cdd_size:
                raise ValueError(f"invalid extent for W9 chunk {chunk:#x}")
            cdd.seek(start)
            data = cdd.read(end - start)
            print(json.dumps({"language": language, "chunk": hex(chunk),
                              "offset": hex(start), "bytes": len(data),
                              "sha256": hashlib.sha256(data).hexdigest()}))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(required=True)
    listing = subparsers.add_parser("list", help="list files on a cue/bin data track")
    listing.add_argument("cue", type=Path)
    listing.set_defaults(func=cmd_list)
    extraction = subparsers.add_parser("extract", help="extract one ISO9660 member")
    extraction.add_argument("cue", type=Path)
    extraction.add_argument("member", help="ISO path, for example /W9.IDX")
    extraction.add_argument("output", type=Path)
    extraction.set_defaults(func=cmd_extract)
    audit = subparsers.add_parser("audit-w9-languages",
                                  help="report hashes for the known PAL language chunks")
    audit.add_argument("index", type=Path, help="locally extracted W9.IDX")
    audit.add_argument("cdd", type=Path, help="locally extracted W9.CDD")
    audit.set_defaults(func=cmd_audit_chunks)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
