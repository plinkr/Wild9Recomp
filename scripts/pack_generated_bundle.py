#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import io
import json
import pathlib
import subprocess
import sys
import tarfile
from typing import cast

SCHEMA = 1
DEFAULT_OUT = "generated-bundle.tar.gz"
JAPANESE_TEXT = "wild9_japanese_text.c"
JP_DISC_SHA256 = "eea7bbe08ac2f650b3e336cd536c00ee78aaa1a07f5b900743aade4853549e09"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text_file(path: pathlib.Path) -> str:
    content = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(content).hexdigest()


def sha1_file(path: pathlib.Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def psxrecomp_sha(root: pathlib.Path) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(root / "psxrecomp"), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"cannot read the psxrecomp revision: {exc}")
    return out.stdout.strip()


def load_identity(root: pathlib.Path) -> dict[str, dict[str, object]]:
    path = root / "catalog_identity.json"
    if not path.is_file():
        raise SystemExit(f"{path} is missing")
    return cast(dict[str, dict[str, object]], json.loads(path.read_text()))


def collect(generated: pathlib.Path) -> dict[str, str]:
    files: dict[str, str] = {}
    for path in sorted(generated.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(generated.parent).as_posix()
        files[rel] = sha256_file(path)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pack generated/ into a bundle with a verification manifest."
    )
    _ = parser.add_argument(
        "--output",
        default=DEFAULT_OUT,
        help=f"archive to write (default: {DEFAULT_OUT})",
    )
    _ = parser.add_argument(
        "--jp-disc",
        help="Wildroid 9 (Japan) .cue: (re)extract the Japanese text into "
        + f"generated/{JAPANESE_TEXT} before packing",
    )
    args = parser.parse_args()
    jp_disc = cast("str | None", args.jp_disc)

    root = pathlib.Path(__file__).resolve().parent.parent
    generated = root / "generated"
    game_toml = root / "game.toml"

    if not (generated / "SLUS_004.25_dispatch.c").is_file():
        raise SystemExit(
            "generated/SLUS_004.25_dispatch.c is missing. Regenerate first:\n"
            + "  python3 psxrecomp/psxrecomp_cli.py generate --config game.toml "
            + "--project-root . --disc '<your Wild 9 .cue>'"
        )
    if not game_toml.is_file():
        raise SystemExit(f"{game_toml} is missing")

    japanese = generated / JAPANESE_TEXT
    if jp_disc:
        _ = subprocess.run(
            [
                sys.executable,
                str(root / "tools" / "localization" / "extract_japanese_text.py"),
                "--disc",
                jp_disc,
                "--output",
                str(japanese),
            ],
            check=True,
        )
    if not japanese.is_file():
        print(
            f"warning: generated/{JAPANESE_TEXT} is missing; builds from this bundle "
            + "show English when Japanese is selected. "
            + "Pass --jp-disc '<Wildroid 9 .cue>'.",
            file=sys.stderr,
        )

    identity = load_identity(root)
    rom = identity["rom_identity"]
    data_track = cast(dict[str, object], rom["data_track"])
    catalog = identity.get("catalog", {})

    psx_sha = psxrecomp_sha(root)
    toml_sha = sha256_text_file(game_toml)
    disc_sha1 = str(data_track["sha1"])
    cue_name = str(rom["cue_name"])
    files = collect(generated)
    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "game": catalog.get("name", "Wild 9"),
        "serial": rom.get("serial", "SLUS-00425"),
        "psxrecomp_sha": psx_sha,
        "game_toml_sha256": toml_sha,
        "disc_sha1": disc_sha1,
        "disc_size": data_track["size"],
        "cue_name": cue_name,
        "created_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "files": files,
    }
    if japanese.is_file():
        manifest["japanese_text_source_sha256"] = JP_DISC_SHA256

    output = pathlib.Path(cast(str, args.output))
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    manifest_bytes = json.dumps(manifest, indent=2, sort_keys=True).encode()

    with tarfile.open(output, "w:gz", compresslevel=9) as archive:
        info = tarfile.TarInfo("generated-bundle.json")
        info.size = len(manifest_bytes)
        info.mtime = int(dt.datetime.now().timestamp())
        info.mode = 0o644

        archive.addfile(info, io.BytesIO(manifest_bytes))
        for path in sorted(generated.rglob("*")):
            if path.is_file():
                archive.add(path, arcname=path.relative_to(generated.parent).as_posix())

    print(f"Wrote {output}")
    print(f"  psxrecomp_sha  {psx_sha}")
    print(f"  game_toml      {toml_sha[:16]}...")
    print(f"  disc           {cue_name} sha1={disc_sha1[:16]}...")
    print(f"  japanese text  {'yes' if japanese.is_file() else 'no'}")
    print(f"  {len(files)} file(s), {output.stat().st_size / 1e6:.1f} MB")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
