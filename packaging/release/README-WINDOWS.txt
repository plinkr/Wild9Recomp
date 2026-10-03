Wild 9 Recompiled - Product Build Quick Start (Windows)
=======================================================

This package is self-contained. You do not need Visual Studio, MSVC, CMake,
Ninja, Python, or any compiler. The game is already compiled into the
executable; there is no build step and no "Generate & rebuild" wizard.


1. Unzip
--------

Extract the complete ZIP into a writable folder. Avoid directories requiring
administrator privileges, such as C:\Program Files.

    Wild9_Recompiled.exe
    assets\   bios\   mods\   saves\   game.toml   input.ini   ...

Do not run directly from inside the ZIP archive.


2. Point it at your disc
------------------------

In the launcher, browse for your Wild 9 disc image:

    Wild 9 (USA).cue   (the .cue must sit next to its .bin)

The launcher checks the disc fingerprint and rejects non-matching revisions.


3. Play
-------

Save states, memory cards, settings, rebinds, and installed mods reside in the
same directory as the executable:

    saves\        memory cards and save states
    mods\         installed mods
    keybinds.ini  controller bindings

Upgrade by extracting newer builds over existing files. Saves and settings
remain intact.


Requirements
------------

64-bit Windows 10 or newer, with an OpenGL 3.3 or Vulkan compatible GPU driver.


Code Signing Notice
-------------------

If Windows SmartScreen or Smart App Control flags the executable on first run,
select Properties > Unblock > Apply. Official builds without a hardware-backed
Authenticode certificate will show this warning.
