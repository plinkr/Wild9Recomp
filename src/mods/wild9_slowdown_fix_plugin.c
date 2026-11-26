#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Wild 9's main loop (0x80010334) is locked to VSync(2), with no frame skip or
 * delta time. It times its own logic and draw-list building between the
 * VSync(1) calls at 0x800108F0 and 0x8001088C, which the debug overlay shows
 * as PROCESSOR USAGE. A frame that runs past two VBlanks takes three, so the
 * whole game drops to 20 fps and two thirds of its speed.
 *
 * [timing] guest_cycle_scale charges each instruction a quarter of its cycles
 * so that work fits again. It can't stay on everywhere: v_wait (0x8005783C)
 * gives up after 32768 passes, barely more than one VBlank at full cost, so
 * cheaper passes turn every VSync wait into a timeout and the game runs too
 * fast. The scale is mod-gated and only opens inside the measured window.
 *
 * The gate opens at the first VBlank of that window, not at its start. Host
 * time tracks the instructions executed, not the cycles charged, so opening
 * it early packed a whole frame's logic into one VBlank and the host dropped
 * frames and audio in exactly the busy areas this mod is for. This way a
 * frame that fits in one VBlank runs as on the original, and a longer one
 * still has room for about five VBlanks of work.
 *
 * Savestates carry the gate state, and a disabled package runs no callbacks.
 * A state saved with the gate open and loaded with the mod off would leave the
 * scale on everywhere, so the hooks are always installed and, with the feature
 * off, they only close the gate.
 */

#define FEATURE_ID "wild9.slowdown_fix"

#define W9_VSYNC_FUNC 0x800576C4u
#define W9_VSYNC_ENTRY_WORD0 0x3C028007u /* lui v0,0x8007 */
#define W9_VSYNC_ENTRY_WORD1 0x8C4241E0u /* lw v0,0x41e0(v0) */

/* Return addresses of main's two VSync(1) calls. */
#define W9_FRAME_BEGIN_RA 0x800108F8u /* g_frame_start_hcount = VSync(1) */
#define W9_FRAME_END_RA 0x80010894u   /* g_dbg_vsync_delta = VSync(1) - start */

/* First call after the gameplay loop exits (also runs once at boot). */
#define W9_LOOP_EXIT_FUNC 0x8001DA00u
#define W9_LOOP_EXIT_ENTRY_WORD0 0x00002021u /* clear a0 */
#define W9_LOOP_EXIT_ENTRY_WORD1 0x3C028008u /* lui v0,0x8008 */

/* Memory card waits run inside the measured window and spin until the card
 * answers, which takes fixed device time. With the gate open they would spin
 * four times as many instructions, so entering one closes the gate for the
 * rest of that frame. */
#define W9_CARD_SW_WAIT_FUNC 0x80055410u /* TestEvent loop, card SW events */
#define W9_CARD_HW_WAIT_FUNC 0x800554C8u /* TestEvent loop, card HW events */
#define W9_CARD_WAIT_ENTRY_WORD0 0x27BDFFE8u /* addiu sp,sp,-0x18 */
#define W9_CARD_WAIT_ENTRY_WORD1 0xAFB00010u /* sw s0,0x10(sp) */
#define W9_BIOS_CARD_WAIT_FUNC 0x8005FB58u   /* _card_wait, B0:5D */
#define W9_BIOS_CARD_WAIT_WORD0 0x240A00B0u  /* li t2,0xb0 */
#define W9_BIOS_CARD_WAIT_WORD1 0x01400008u  /* jr t2 */

#define W9_VSYNC_MODE_HBLANK_COUNT 1

static int s_feature_active = 0;
/* Inside the measured window; the next VBlank opens the gate. */
static int s_in_frame = 0;

static void wild9_slowdown_fix_close(void) {
  s_in_frame = 0;
  psx_mod_set_guest_cycle_scale_gate(0);
}

static void wild9_slowdown_fix_activate(void) { s_feature_active = 1; }

static void wild9_slowdown_fix_session_reset(void) {
  s_feature_active = 0;
  s_in_frame = 0;
}

/* A restored state keeps its own gate. Tracking restarts at the next frame,
 * so at worst the frame in progress runs unscaled. */
static void wild9_slowdown_fix_savestate(void) { s_in_frame = 0; }

static void wild9_slowdown_fix_vblank(void) {
  if (s_feature_active && s_in_frame)
    psx_mod_set_guest_cycle_scale_gate(1);
}

static int wild9_slowdown_fix_vsync_prehook(struct CPUState *cpu) {
  const int32_t mode = (int32_t)cpu->gpr[4];
  const uint32_t ra = cpu->gpr[31];

  if (!s_feature_active) {
    wild9_slowdown_fix_close();
  } else if (mode == W9_VSYNC_MODE_HBLANK_COUNT) {
    if (ra == W9_FRAME_BEGIN_RA) {
      wild9_slowdown_fix_close();
      s_in_frame = 1;
    } else if (ra == W9_FRAME_END_RA) {
      wild9_slowdown_fix_close();
    }
  } else if (mode >= 0) {
    /* VSync(0) and VSync(n) wait in v_wait; negative modes only read the
     * VBlank count and are used by time-based CD and GPU timeouts. */
    wild9_slowdown_fix_close();
  }
  return 0;
}

static int wild9_slowdown_fix_card_wait_prehook(struct CPUState *cpu) {
  (void)cpu;
  wild9_slowdown_fix_close();
  return 0;
}

static int wild9_slowdown_fix_loop_exit_prehook(struct CPUState *cpu) {
  (void)cpu;
  wild9_slowdown_fix_close();
  return 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_slowdown_fix_plugin) {
  static const uint32_t vsync_entry[2] = {W9_VSYNC_ENTRY_WORD0,
                                          W9_VSYNC_ENTRY_WORD1};
  static const uint32_t loop_exit_entry[2] = {W9_LOOP_EXIT_ENTRY_WORD0,
                                              W9_LOOP_EXIT_ENTRY_WORD1};
  static const uint32_t card_wait_entry[2] = {W9_CARD_WAIT_ENTRY_WORD0,
                                              W9_CARD_WAIT_ENTRY_WORD1};
  static const uint32_t bios_card_wait_entry[2] = {W9_BIOS_CARD_WAIT_WORD0,
                                                   W9_BIOS_CARD_WAIT_WORD1};

  (void)psx_mod_register_activation_plugin(FEATURE_ID,
                                           wild9_slowdown_fix_activate);
  /* Also runs for plans without this plugin, which clears the feature flag. */
  (void)psx_mod_register_session_reset_plugin(FEATURE_ID,
                                              wild9_slowdown_fix_session_reset);
  (void)psx_mod_register_savestate_plugin(FEATURE_ID,
                                          wild9_slowdown_fix_savestate);
  (void)psx_mod_register_vblank_plugin(FEATURE_ID, wild9_slowdown_fix_vblank);

  /* Every hook declines, so the originals run and charge their own cycles. */
  (void)func_override_add_guarded(FEATURE_ID ":vsync", W9_VSYNC_FUNC,
                                  wild9_slowdown_fix_vsync_prehook, vsync_entry,
                                  2, 0);
  (void)func_override_add_guarded(FEATURE_ID ":loop_exit", W9_LOOP_EXIT_FUNC,
                                  wild9_slowdown_fix_loop_exit_prehook,
                                  loop_exit_entry, 2, 0);
  (void)func_override_add_guarded(
      FEATURE_ID ":card_sw_wait", W9_CARD_SW_WAIT_FUNC,
      wild9_slowdown_fix_card_wait_prehook, card_wait_entry, 2, 0);
  (void)func_override_add_guarded(
      FEATURE_ID ":card_hw_wait", W9_CARD_HW_WAIT_FUNC,
      wild9_slowdown_fix_card_wait_prehook, card_wait_entry, 2, 0);
  (void)func_override_add_guarded(
      FEATURE_ID ":bios_card_wait", W9_BIOS_CARD_WAIT_FUNC,
      wild9_slowdown_fix_card_wait_prehook, bios_card_wait_entry, 2, 0);
  func_override_install();
}
