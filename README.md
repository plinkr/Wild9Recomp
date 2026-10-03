# Wild 9 Recompiled

Static recompilation and native PC port of Wild 9 (PlayStation 1, NTSC-U SLUS-00425).

[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/plinkr/Wild9Recomp/total)](https://github.com/plinkr/Wild9Recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/plinkr/Wild9Recomp/latest/total)](https://github.com/plinkr/Wild9Recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/plinkr/Wild9Recomp)](https://github.com/plinkr/Wild9Recomp/releases/latest)

<p align="center">
  <img src="launcher_assets/img/boxart.png" alt="Wild 9 Box Art" width="280">
</p>

<div align="center">
  <p style="max-width:900px; margin:0 auto;">Screenshots (click a thumbnail to view full size):</p>
  <div style="margin-top:12px; overflow-x:auto; white-space:nowrap; padding:8px 4px; -webkit-overflow-scrolling:touch;">
    <a href="https://github.com/user-attachments/assets/7784e243-98aa-4e82-9107-4bdabb7146bc" target="_blank" rel="noopener">
      <img src="https://github.com/user-attachments/assets/7784e243-98aa-4e82-9107-4bdabb7146bc" width="280" style="display:inline-block; margin-right:8px; border-radius:8px; box-shadow:0 6px 18px rgba(0,0,0,0.12);" alt="Level 4" />
    </a>
    <a href="https://github.com/user-attachments/assets/5e9e5a1a-76ab-438c-8e88-df57aa4a49e4" target="_blank" rel="noopener">
      <img src="https://github.com/user-attachments/assets/5e9e5a1a-76ab-438c-8e88-df57aa4a49e4" width="280" style="display:inline-block; margin-right:8px; border-radius:8px; box-shadow:0 6px 18px rgba(0,0,0,0.12);" alt="Level 1" />
    </a>
  </div>
</div>


## Overview

Wild 9 is a 2.5D platform action game developed by Shiny Entertainment and published by Interplay in 1998 for the Sony PlayStation. This project provides a native PC port of the original NTSC-U release (SLUS-00425) by statically translating the MIPS R3000A machine code from the game executable into native C source files.

The recompiled C code links directly against a hardware-accurate runtime environment provided by [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp) and [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui). The runtime provides native display output, widescreen viewport expansion, PGXP geometric precision correction, an integrated OpenBIOS implementation, and modding plugins while preserving the original game logic, physics, and timing.

## Features

- Static binary recompilation: Translates the original SLUS_004.25 executable directly into native C code.
- Native PC execution: Runs natively on 64-bit Linux, Windows, and macOS without an emulator.
- Integrated OpenBIOS: Ships with the open-source PCSX-Redux OpenBIOS implementation; proprietary Sony BIOS dumps are optional.
- Geometry and texturing fixes: Employs PGXP precision geometry and perspective-correct texturing to eliminate PlayStation affine texture warping and polygon jitter.
- Widescreen rendering: Native widescreen expansion with automated 2D backdrop stretching and SXY screen culling adjustments.
- Frame rate presentation mod: Optional temporal frame blending mod supporting 60, 120, 144, 165 FPS, and display refresh targets on OpenGL while maintaining original 59.94 Hz simulation cadence.
- Video skip mod: Configurable controller button shortcut to skip introductory FMV sequences and logos.
- Debug overlay: Mod plugin exposing the native engine diagnostic overlay for memory, GPU, and rendering performance metrics.

---

## Getting Started (How to Play)

This release embeds OpenBIOS (open-source PS1 BIOS from PCSX-Redux), removing the requirement for a proprietary PlayStation BIOS dump. Retail SCPH dumps can still be placed in `bios/` if authentic behavior is preferred.

### Self-Contained Builds (No Toolchain Required)

These are the **product** builds: the game code is already compiled into the
executable and the first-run setup wizard is compiled out, so there is no build
step, no "Generate & rebuild", and nothing to install. Every release publishes
all five assets:

| Platform | Asset | Notes |
| --- | --- | --- |
| Linux | `wild9-<version>-linux-x86_64.AppImage` | `chmod +x` and run. Self-contained; needs only glibc 2.28+ and a GL 3.3 / Vulkan driver. |
| Linux | `wild9-<version>-linux-x64.zip` | Portable folder: unzip and run `Wild9_Recompiled`. |
| Windows | `wild9-<version>-windows-x64.zip` | Unzip anywhere writable and run `Wild9_Recompiled.exe`. No Python, no MSVC, no compiler. Authenticode-signed when the release is built with a signing certificate. |
| macOS (Apple silicon) | `wild9-<version>-macos-arm64.zip` | Unzip and run `Wild9_Recompiled`. |
| macOS (Intel) | `wild9-<version>-macos-x64.zip` | Unzip and run `Wild9_Recompiled`. |

The Windows zip is a portable build. psxrecomp defaults `PSX_STATIC_RUNTIME` to `ON`
for MinGW Release builds, statically linking SDL, libgcc, libstdc++, and zlib so
the executable imports nothing outside System32. `tools/package_product_zip.sh`
verifies the PE import table with `objdump` and stages sibling DLLs left by the build.
The build deliberately avoids installing a system SDL3 DLL so that SDL3 is linked
statically from source.

1. Download the asset for your platform.
2. Run the executable, browse for your Wild 9 `.cue`, and press Play.

The AppImage keeps persistent user data outside the read-only mount in
`$XDG_DATA_HOME/Wild9Recomp/` (`~/.local/share/Wild9Recomp`), so upgrades never
overwrite saves. Set `WILD9RECOMP_DATA_DIR` to relocate it. The ZIPs are
portable -- saves, memory cards, settings, and mods live beside the executable.

### Prerequisites

- Legally owned copy of **Wild 9 (USA)** in `.cue` format.
- Keep the `.cue` and referenced `.bin` data tracks in the same directory.

The `.cue` is only used as the disc data source at runtime. The game code itself
is already compiled into the executable, so nothing is generated or rebuilt on
the player's machine.

### Platform Notes

- **Linux (AppImage):** No compiler or external tools required. `chmod +x` and run.
- **Linux (ZIP):** Portable directory: unzip and run `Wild9_Recompiled`.
- **Windows (ZIP):** No compiler, Python or MSVC required. Unzip anywhere writable and run `Wild9_Recompiled.exe`.
- **macOS (Apple silicon / Intel):** No compiler required. Unzip and run `Wild9_Recompiled`.

---

## Cheats

### In-Game Controller Codes

Original cheat sequences can be entered on the controller while paused:
- Full Health: Pause, then press `R1`, `Triangle`, `L1`, `Left`, `Triangle`, `Circle`, `Cross`.
- Unlock All Levels: Pause, then press `Up`, `Left`, `Down`, `R2`, `Right`, `Square`, `Cross`.
- Red Beam Mode: Pause, then press `Right`, `Up`, `Left`, `Circle`, `Up`, `Circle`, `Circle`.
- Ten Additional Grenades: Pause, then press `R1`, `Cross`, `R1`, `Right`, `Square`, `Right`, `Square`.
- Ten Additional Missiles: Pause, then press `Cross`, `Circle`, `R1`, `Right`, `Triangle`, `Cross`, `Triangle`.

### Built-in Cheats via Launcher (Mods)

> [!WARNING]
> Enabling GameShark-style memory cheats may trigger unexpected glitches or sequence breaks.

Cheats can also be toggled from the Mods tab in the launcher before starting the game:

**General:**
- **Infinite Lives:** Restores life count to 3 when lost.
- **Infinite Health:** Restores health to 16 upon taking damage.
- **Infinite Missiles on Pick-Up:** Locks missile ammo to 3 once collected.
- **Moon Jump:** Hold Jump (`X`) to ascend continuously.

**Drench Level Codes:**
- **Infinite Health (Drench):** Restores health to 17 during the Drench stage.
- **Moon Jump (Drench):** Hold Jump (`X`) to ascend continuously during Drench.

All cheats are disabled by default.

---

### Preloaded Mods

Configured from the Mods tab in the launcher:

- Widescreen (`wild9.enhancement.widescreen`):
  Expands horizontal field of view to 16:9 during gameplay, adjusting 2D background tile column counts, camera clip bounds, and primitive culling margins.
- Wild 9 Frame Rate (`wild9.enhancement.frame-interpolation`):
  Enables OpenGL temporal frame blending between completed frames. Configurable to 60, 120, 144, 165 FPS, or unconstrained display refresh. Game simulation remains locked to 59.94 Hz.
- FMV Skip (`wild9.enhancement.fmvskip`):
  Skips intro videos on button press (defaults to `Start`, configurable to `Cross`, `Square`, `Triangle`, or `Select`).
- Debug Overlay (`wild9.debug.overlay`):
  Activates the game's internal diagnostic overlay displaying CPU, GPU, memory, and draw call counters.
- Gameplay Cheats (`wild9.gameplay.cheats`):
  Individual toggles for infinite lives, infinite health, infinite missiles, and moon jump.

---

## RetComM Launcher

This title can also be managed through **[RetComM Launcher](https://github.com/TechnicallyComputers/RetComM-Launcher)** for unified updates, build queueing, and shared toolchains across recompilation projects.

---

## Building from Source

To build the project from source:

```bash
# Clone repository and submodules
git clone --recursive https://github.com/plinkr/Wild9Recomp.git
cd Wild9Recomp

# Update submodules
git submodule update --init --recursive

# Generate recompiled game code from disc image
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml \
  --project-root . \
  --disc "disc/Wild 9 (USA).cue"

# Build runtime
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

### Packaging Product Builds

`tools/package_appimage.sh` and `tools/package_product_zip.sh` produce self-contained
artifacts in `dist/`. Both link the precompiled game C and exclude the setup wizard:

```bash
sh tools/package_appimage.sh        # -> dist/wild9-<v>-linux-x86_64.AppImage
sh tools/package_product_zip.sh     # -> dist/wild9-<v>-<host>.zip
```

`package_product_zip.sh` names the artifact after the build host. Under `OS=Windows_NT`,
it performs PE import verification and Authenticode signing.

Both tools require generated game sources (`python3 psxrecomp/psxrecomp_cli.py generate`),
CMake, Ninja, a C/C++ compiler, Python 3, and ImageMagick (for AppImage packaging).
`tools/product_build_verify.sh` verifies symbol presence (`func_80010000`, `func_8005F7C8`,
`OpenBIOS_psx_bios_backend`) and asserts absence of local codegen host markers.

### Supplying generated/ to CI

Product builds require recompiled game C sources rather than raw disc images.
`scripts/pack_generated_bundle.py` writes `generated-bundle.tar.gz` containing
the generated C files and a manifest pinning the `psxrecomp` commit SHA, `game.toml`
hash, and disc SHA-1:

```bash
python3 scripts/pack_generated_bundle.py
```

CI product jobs consume the bundle via repository secrets:

| Secret | Value |
| --- | --- |
| `PSXRECOMP_GENERATED_BUNDLE_URL` | Direct URL to `generated-bundle.tar.gz` |
| `PSXRECOMP_BUNDLE_TOKEN` | Read token for bundle repository access |

`scripts/fetch_generated_bundle.py` validates archive contents against manifest
hashes and drops authorization headers if redirected across hosts.

### Overlay Architecture

Wild 9 does not utilize disc overlay executables. The entire game logic resides in
the 425,984-byte boot executable `SLUS_004.25`, with level streaming and data stored
in `W9.CDD` and `W9.IDX`. Consequently, `packaging/release/game.toml` explicitly sets
`overlay_cache = false` and skips runtime background compilation threads.

---

## Licenses & Credits

- **Repository Code:** Licensed under the **MIT License**. See [LICENSE](LICENSE).
- **Framework:** [PSXRecomp](https://github.com/RetroPortingToolKit/psxrecomp) is licensed under the **PolyForm Noncommercial 1.0.0 License**. Copyright (c) 2026 Matthew Stanley. Commercial licensing: [https://1379.tech](https://1379.tech).
- **Launcher:** [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) is licensed under the **MIT License**. Copyright (c) 2026 Matthew Stanley.
- **BIOS:** [OpenBIOS](https://github.com/grumpycoders/pcsx-redux) from PCSX-Redux is licensed under the **MIT License**. Copyright (c) 2019 PCSX-Redux authors.

### Disclaimer

This is an unofficial fan recompilation project. Wild 9 is a trademark of Shiny Entertainment and Interplay. No copyrighted game assets or proprietary Sony PlayStation BIOS dumps are bundled or distributed.
