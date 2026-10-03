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

SCHEMA = 1
DEFAULT_OUT = "generated-bundle.tar.gz"


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


def load_identity(root: pathlib.Path) -> dict:
    path = root / "catalog_identity.json"
    if not path.is_file():
        raise SystemExit(f"{path} is missing")
    return json.loads(path.read_text())


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
    parser.add_argument(
        "--output", default=DEFAULT_OUT, help=f"archive to write (default: {DEFAULT_OUT})"
    )
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    generated = root / "generated"
    game_toml = root / "game.toml"

    if not (generated / "SLUS_004.25_dispatch.c").is_file():
        raise SystemExit(
            "generated/SLUS_004.25_dispatch.c is missing. Regenerate first:\n"
            "  python3 psxrecomp/psxrecomp_cli.py generate --config game.toml "
            "--project-root . --disc '<your Wild 9 .cue>'"
        )
    if not game_toml.is_file():
        raise SystemExit(f"{game_toml} is missing")

    identity = load_identity(root)
    rom = identity["rom_identity"]

    manifest = {
        "schema": SCHEMA,
        "game": identity.get("catalog", {}).get("name", "Wild 9"),
        "serial": rom.get("serial", "SLUS-00425"),
        "psxrecomp_sha": psxrecomp_sha(root),
        "game_toml_sha256": sha256_text_file(game_toml),
        "disc_sha1": rom["data_track"]["sha1"],
        "disc_size": rom["data_track"]["size"],
        "cue_name": rom["cue_name"],
        "created_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "files": collect(generated),
    }

    output = pathlib.Path(args.output)
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
    print(f"  psxrecomp_sha  {manifest['psxrecomp_sha']}")
    print(f"  game_toml      {manifest['game_toml_sha256'][:16]}...")
    print(f"  disc           {manifest['cue_name']} sha1={manifest['disc_sha1'][:16]}...")
    print(f"  {len(manifest['files'])} file(s), {output.stat().st_size / 1e6:.1f} MB")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
