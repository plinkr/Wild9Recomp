# Wild 9 Recompiled AppImage Notes

## Overview

A self-contained Linux AppImage for Wild 9 Recompiled. The recompiled game code
is linked directly into the binary, requiring no external compiler or local
recompilation toolchain.

## Execution

Make the file executable and run:

```bash
chmod +x wild9-<version>-linux-x86_64.AppImage
./wild9-<version>-linux-x86_64.AppImage
```

Select a verified `Wild 9 (USA).cue` disc image from the launcher interface.
The `.cue` file and its corresponding `.bin` data tracks must reside in the
same directory.

## Data Directory

User data, saves, configuration, and mods are stored outside the read-only
AppImage mount:

```
$XDG_DATA_HOME/Wild9Recomp/ (default: ~/.local/share/Wild9Recomp)
```

Set the `WILD9RECOMP_DATA_DIR` environment variable to override this path.

## Requirements

- x86-64 Linux
- glibc 2.28 or newer
- OpenGL 3.3 or Vulkan driver support

## Overlay Subsystem

Wild 9 uses a single static executable (`SLUS_004.25`) with all game assets
and streaming data indexed in external data archives (`W9.CDD`, `W9.IDX`).
The runtime disables runtime overlay recompilation (`overlay_cache = false`).

## Rebuilding

The AppImage can be built locally using:

```bash
sh tools/package_appimage.sh
```

Building requires CMake, Ninja, a C/C++ compiler, Python 3, ImageMagick, and
pre-generated game C sources.
