# Wild 9 Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/plinkr/Wild9Recomp/total)](https://github.com/plinkr/Wild9Recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/plinkr/Wild9Recomp/latest/total)](https://github.com/plinkr/Wild9Recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/plinkr/Wild9Recomp)](https://github.com/plinkr/Wild9Recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

<p align="center">
  <img src="launcher_assets/img/boxart.png" alt="Wild 9 Box Art" width="280">
</p>

<div align="center">
  <p style="max-width:900px; margin:0 auto;">Screenshots (click a thumbnail to view full size):</p>
  <div style="margin-top:12px; overflow-x:auto; white-space:nowrap; padding:8px 4px; -webkit-overflow-scrolling:touch;">
    <a href="https://github.com/user-attachments/assets/7784e243-98aa-4e82-9107-4bdabb7146bc" target="_blank" rel="noopener">
      <img src="https://github.com/user-attachments/assets/7784e243-98aa-4e82-9107-4bdabb7146bc" width="280" style="display:inline-block; margin-right:8px; border-radius:8px; box-shadow:0 6px 18px rgba(0,0,0,0.12);" alt="Level 1" />
    </a>
    <a href="https://github.com/user-attachments/assets/5e9e5a1a-76ab-438c-8e88-df57aa4a49e4" target="_blank" rel="noopener">
      <img src="https://github.com/user-attachments/assets/5e9e5a1a-76ab-438c-8e88-df57aa4a49e4" width="280" style="display:inline-block; margin-right:8px; border-radius:8px; box-shadow:0 6px 18px rgba(0,0,0,0.12);" alt="Level 4" />
    </a>
  </div>
</div>

**Wild 9 Recompiled**

This project is a native PC port of the classic PlayStation title **Wild 9** (1998, developed by Shiny Entertainment and published by Interplay). It is built through static recompilation powered by [psxrecomp](https://github.com/mstan/psxrecomp) and [recomp-ui](https://github.com/mstan/recomp-ui).

Rather than running an emulator, this statically translates the original MIPS game binary directly into native C. The result is smooth performance, true widescreen display, high-resolution rendering, PGXP precision geometry correction that fixes original PS1 polygon jitter and texture distortion, while preserving the original game physics and mechanics.

| Title | Wild 9 |
|---|---|
| Platform | PC (Windows / Linux / macOS) |
| Original Release | 1998 (Interplay / Shiny Entertainment) |
| Region | NTSC-U (USA - SLUS-00425) |
| Enhancements | Native 60 FPS, Widescreen, PGXP Precision Geometry, OpenBIOS |

---

## Getting Started (How to Play)

Getting the game running is quick and painless, **this release uses OpenBIOS** (the free, open-source PS1 BIOS from PCSX-Redux), meaning you do **not** need to hunt down or dump a proprietary Sony BIOS to start playing. (Though you can still provide an authentic retail SCPH dump if you prefer).

### Prerequisites
- A legally owned copy of **Wild 9 (USA)** in `.cue` format.
- Keep your `.cue` and all referenced `.bin` / audio tracks together in the same directory.

### Setup Steps

1. **Extract the archive:** Extract the complete setup ZIP into a normal, writable folder (avoid directories that require administrator rights like `C:\Program Files`).
2. **Launch the setup wizard:** Start `Wild9_Recompiled` (`Wild9_Recompiled.exe` on Windows).
3. **Select your disc image:** In the wizard, browse and select your Wild 9 `.cue` file.
4. **BIOS:** OpenBIOS is built-in and selected by default, so you're ready to go immediately without hunting for external BIOS files.
5. **Generate & rebuild:** Click **Generate & rebuild** and wait for the recompilation to finish. The game will automatically boot once it's done.

### Platform Notes

- **Windows:** The setup wizard can automatically download the portable build tools for you—no manual compiler setup required.
- **Linux & macOS:** Please install standard build tools prior to running: CMake, Ninja, Python 3, and a C/C++ compiler (`gcc` or `clang`).

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
- **Framework:** [PSXRecomp](https://github.com/mstan/psxrecomp) is licensed under the **PolyForm Noncommercial 1.0.0 License**. See its `LICENSE` file. Copyright © 2026 Matthew Stanley; commercial licensing inquiries go to him at [https://1379.tech](https://1379.tech).
- **Launcher:** [recomp-ui](https://github.com/mstan/recomp-ui) is licensed under the **MIT License**. Copyright (c) 2026 Matthew Stanley.
- **BIOS:** This release uses [OpenBIOS](https://github.com/grumpycoders/pcsx-redux) from the PCSX-Redux project, licensed under the **MIT License** (Copyright (c) 2019 PCSX-Redux authors).

Their licenses and dependency notices remain in the corresponding source directories.

### Disclaimer

This is an unofficial fan recompilation project. The original game and its trademarks belong to their respective owners (Shiny Entertainment / Interplay). No proprietary game assets or copyrighted Sony BIOS dumps are bundled or distributed with this repository.
