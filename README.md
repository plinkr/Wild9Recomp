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

Getting the game running is quick and painless, **this release uses OpenBIOS** (the free, open-source PS1 BIOS from PCSX-Redux), meaning you do **not** need to hunt down or dump a proprietary Sony BIOS to start playing. (Though you can still provide an authentic retail SCPH dump if you prefer).

### Prerequisites
- A legally owned copy of **Wild 9 (USA)** in `.cue` format.
- Keep your `.cue` and the referenced `.bin` / audio tracks together in the same directory.

### Setup Steps

1. **Extract the archive:** Extract the complete setup ZIP into a normal, writable folder (avoid directories that require administrator rights like `C:\Program Files`).
2. **Launch the setup wizard:** Start `Wild9_Recompiled` (`Wild9_Recompiled.exe` on Windows).
3. **Select your disc image:** In the wizard, browse and select your Wild 9 `.cue` file.
4. **BIOS:** OpenBIOS is built-in and selected by default, so you're ready to go immediately without hunting for external BIOS files.
5. **Generate & rebuild:** Click **Generate & rebuild** and wait for the recompilation to finish. The game will automatically boot once it's done.

### Platform Notes

- **Windows:** The setup wizard can automatically download the portable build tools for you, no manual compiler setup required.
- **Linux & macOS:** Please install standard build tools prior to running: CMake, Ninja, Python 3.12 or newer, and a C/C++ compiler (`gcc` or `clang`).

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
> **Warning!** Enabling these types of mods/cheats (similar to GameShark codes) **WILL BREAK THE GAME** or cause unexpected glitches/behavior during gameplay. Use them at your own risk.

Alternatively, you don't have to enter button codes every time. When you start the game executable (`Wild9_Recompiled` or `Wild9_Recompiled.exe` on Windows), go to the **Mods** tab in the launcher before booting the game. You can enable the **Gameplay Cheats** mod and toggle individual cheat options:

**General:**
- **Infinite Lives:** Automatically restores your lives count to 3 when lost.
- **Infinite Health:** Automatically restores health back to full (16) upon taking damage.
- **Infinite Missiles on Pick-Up:** Keeps missile ammunition at 3 once you pick them up.
- **Moon Jump:** Hold Jump (`X`) to float and ascend indefinitely.

**Drench Level Codes:**
- **Infinite Health (Drench):** Automatically restores health back to maximum (17) in the Drench level.
- **Moon Jump (Drench):** Hold Jump (`X`) to float and ascend indefinitely in the Drench level.

All cheats are disabled by default so you can choose exactly which ones to enable for your playthrough.

---

### Preloaded Mods

Enable and configure mod plugins from the Mods tab in the launcher:

- Widescreen (`wild9.enhancement.widescreen`):
  Expands the horizontal field of view to 16:9 during gameplay, adjusting 2D background tile column counts, camera clip bounds, and primitive culling margins.
- Wild 9 Frame Rate (`wild9.enhancement.frame-interpolation`):
  Enables OpenGL temporal frame blending between completed frames. Configurable to 60, 120, 144, 165 FPS, or unconstrained display refresh. Game physics, timers, and audio remain locked to 59.94 Hz.
- FMV Skip (`wild9.enhancement.fmvskip`):
  Skips intro videos on button press (defaults to `Start`, configurable to `Cross`, `Square`, `Triangle`, or `Select`).
- Debug Overlay (`wild9.debug.overlay`):
  Activates the game's internal diagnostic overlay displaying CPU, GPU, memory, and draw call counters.
- Gameplay Cheats (`wild9.gameplay.cheats`):
  Individual toggles for infinite lives, infinite health, infinite missiles, and moon jump.

---

## RetComM Launcher

If you have multiple recomp titles or prefer a unified manager, you can also launch and manage this game with **[RetComM Launcher](https://github.com/TechnicallyComputers/RetComM-Launcher)**. It handles automated updates, queued builds, and toolchain sharing without having to repeat the setup wizard manually.

---

## Building from Source (Developers)

If you want to build or tinker with the project directly from source:

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/plinkr/Wild9Recomp.git
cd Wild9Recomp

# Update submodules
git submodule update --init --recursive

# Generate recompiled game code from your disc image
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml \
  --project-root . \
  --disc "disc/Wild 9 (USA).cue"

# Build runtime
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

---

## Licenses & Credits

This project is made possible thanks to incredible upstream work:

- **Original Project Code:** Any code created by me in this repository is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.
- **Framework:** [PSXRecomp](https://github.com/RetroPortingToolKit/psxrecomp) is licensed under the **PolyForm Noncommercial 1.0.0 License**. See its `LICENSE` file. Copyright © 2026 Matthew Stanley; commercial licensing inquiries go to him at [https://1379.tech](https://1379.tech).
- **Launcher:** [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) is licensed under the **MIT License**. Copyright (c) 2026 Matthew Stanley.
- **BIOS:** This release uses [OpenBIOS](https://github.com/grumpycoders/pcsx-redux) from the PCSX-Redux project, licensed under the **MIT License** (Copyright (c) 2019 PCSX-Redux authors).

Their licenses and dependency notices remain in the corresponding source directories.

### Disclaimer

This is an unofficial fan recompilation project. The original game and its trademarks belong to their respective owners (Shiny Entertainment / Interplay). No proprietary game assets or copyrighted Sony BIOS dumps are bundled or distributed with this repository.
