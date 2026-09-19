#include "mod_plugins.h"

/*
 * Wild 9 USA - Debug Overlay Patcher
 *
 * Original instruction:
 *
 *   80010828  29 48 00 0C
 *              jal 0x800120A4
 *
 * Patched instruction:
 *
 *   80010828  8E 4A 00 0C
 *              jal 0x80012A38
 *
 * GameShark equivalent:
 *
 *   80010828 4A8E
 *   8001082A 0C00
 *
 * 0x0C004A8E = JAL 0x80012A38
 *
 * The function at 0x80012A38 is the debug overlay routine
 * that draws:
 *   - MEMORY LEFT
 *   - PROCESSOR USAGE
 *   - GPU USAGE
 *   - MODELS PROCESSED
 *   - MODELS ON SCREEN
 *   - ACTORS PROCESSED
 *   - ACTOR POLYS ON SCREEN
 *   - WORLD POLYS ON SCREEN
 *   - POLYS IN WORLD
 *   - MODELS IN WORLD
 *   - TOTAL PATH POINTS
 *   - WEX path information
 */

/* Address of the instruction being replaced. */
#define W9_DEBUG_PATCH_ADDR 0x80010828u

/* Halfwords making up: jal 0x80012A38 */
#define W9_DEBUG_PATCH_LO 0x4A8Eu
#define W9_DEBUG_PATCH_HI 0x0C00u

static void wild9_debug_overlay_patch(void) {
  psx_mod_write_half(W9_DEBUG_PATCH_ADDR, W9_DEBUG_PATCH_LO);
  psx_mod_write_half(W9_DEBUG_PATCH_ADDR + 2u, W9_DEBUG_PATCH_HI);
}

static void wild9_debug_overlay_activate(void) { wild9_debug_overlay_patch(); }

static void wild9_debug_overlay_vblank(void) {
  if (!psx_mod_game_started())
    return;

  wild9_debug_overlay_patch();
}

PSX_MOD_CONSTRUCTOR(wild9_register_debug_overlay_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.debug_overlay",
                                           wild9_debug_overlay_activate);

  (void)psx_mod_register_vblank_plugin("wild9.debug_overlay",
                                       wild9_debug_overlay_vblank);
}