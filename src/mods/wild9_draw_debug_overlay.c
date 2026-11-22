#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Wild 9 USA still contains its debug overlay routine (0x80012A38), which
 * draws memory left, processor and GPU usage, model, actor and polygon counts
 * and WEX path information, but nothing in the game calls it.
 *
 * The previous GameShark approach (80010828 4A8E / 8001082A 0C00) turns main's
 * call to the pause handler (jal 0x800120A4) into jal 0x80012A38. That shows
 * the overlay but the pause handler never runs again, so Start no longer
 * pauses, and the quit confirmation and the "controller not supported" screen
 * are lost too.
 *
 * Instead of patching the call, this hook runs the overlay just before the
 * pause handler and then lets the handler run as usual.
 */

#define W9_PAUSE_HANDLER_FUNC 0x800120A4u
#define W9_PAUSE_HANDLER_ENTRY_WORD 0x27BDFFD8u /* addiu sp,sp,-0x28 */
#define W9_DEBUG_OVERLAY_FUNC 0x80012A38u

static int wild9_debug_overlay_prehook(struct CPUState *cpu) {
  (void)psx_mod_call_guest(cpu, W9_DEBUG_OVERLAY_FUNC, cpu->gpr[31], 0u, 0u, 0u,
                           0u);
  return 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_debug_overlay_plugin) {
  static const uint32_t pause_handler_entry[1] = {W9_PAUSE_HANDLER_ENTRY_WORD};

  /* Always declines, so the pause handler runs and charges its own cycles;
   * the overlay charges its own by executing. */
  (void)psx_mod_register_function_override(
      "wild9.debug_overlay", W9_PAUSE_HANDLER_FUNC, wild9_debug_overlay_prehook,
      pause_handler_entry, 1, 0);
}
