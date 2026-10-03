Wild 9 Recompiled - Product Build Quick Start (Linux / macOS)
============================================================

This package is self-contained. You do not need CMake, Ninja, Python, or any
compiler. The game is already compiled into the executable; there is no build
step and no "Generate & rebuild" wizard.


1. Unpack
---------

Extract the complete ZIP into a writable folder. It contains:

    Wild9_Recompiled
    assets/   bios/   mods/   saves/   game.toml   input.ini   ...

Run it:

    chmod +x Wild9_Recompiled
    ./Wild9_Recompiled

A Linux AppImage is published separately and needs no extraction at all.


2. Point it at your disc
------------------------

In the launcher, browse for your Wild 9 disc image:

    Wild 9 (USA).cue   (the .cue must sit next to its .bin)

The launcher checks the disc fingerprint and rejects non-matching revisions.
The disc image is only read as game data; nothing is compiled from it.


3. Play
-------

Save states, memory cards, settings, rebinds, and installed mods reside in the
same directory as the executable:

    saves/        memory cards and save states
    mods/         installed mods
    keybinds.ini  controller bindings

Upgrade by extracting newer builds over existing files. Saves and settings
remain intact.


Requirements
------------

64-bit Linux (glibc 2.28+) or macOS 11 or newer, with an OpenGL 3.3 or Vulkan
compatible GPU driver.
