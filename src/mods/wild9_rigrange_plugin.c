#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * The Rig beam has no length constant. Each frame the beam update
 * (0x8002AC5C) emits one particle into a 32-slot trail (0x8004C4DC) with a
 * velocity of player+0x198 (250 units/frame for Wex) plus 0..63 random units,
 * and every particle then loses 1/16 of its velocity per frame. Grab boxes sit
 * on the particles up to 18 frames old, so both the visible beam and its reach
 * scale linearly with that launch velocity. Measured in vanilla: boxes reach
 * ~3100 units, the tip ~4100, grabs land at ~4200-4600. At 200%: ~6200, ~8000
 * and up to ~8000.
 *
 * player+0x198 is the actor's movement speed as well, so the velocity is scaled
 * at the emitter call instead. The particle's width vector is left untouched.
 *
 * Once something is held, 0x8002B2AC releases it when the Rig-to-object
 * distance passes 6000 units. A longer beam grabs beyond that, so the limit
 * scales by the same factor through [mod_immediate] group 1 in game.toml.
 */

#define PKG_RIG_RANGE "wild9.gameplay.rig-range"
#define FEATURE_RIG_RANGE "rig_range"
#define PLUGIN_RIG_RANGE "wild9.rigrange"

#define W9_TRAIL_EMIT_FUNC 0x8004C4DCu
#define W9_TRAIL_EMIT_ENTRY_WORD 0x27BDFFD8u /* addiu sp,sp,-0x28 */
#define W9_BEAM_EMIT_RETURN 0x8002B150u      /* after jal at 0x8002B148 */

/* Offsets in the particle template the beam update passes in a1. */
#define W9_TEMPLATE_RADIAL_VEL 0x18u
#define W9_TEMPLATE_VERTICAL_VEL 0x1Cu

#define W9_RELEASE_IMM_GROUP 1u

#define W9_PERCENT_STOCK 100
#define W9_PERCENT_MAX 200
#define W9_PERCENT_STEP 10

static int s_percent = W9_PERCENT_STOCK;

static int w9_percent_option(void) {
  char text[16] = "";
  const char *p = text;
  int value = 0;

  if (!psx_mod_option_value(PKG_RIG_RANGE, FEATURE_RIG_RANGE, "range_percent",
                            text, sizeof(text)))
    return W9_PERCENT_STOCK;
  if (*p < '0' || *p > '9')
    return W9_PERCENT_STOCK;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10 + (*p - '0');
    if (value > W9_PERCENT_MAX)
      return W9_PERCENT_STOCK;
  }
  if (*p != '\0' || value < W9_PERCENT_STOCK ||
      (value - W9_PERCENT_STOCK) % W9_PERCENT_STEP != 0)
    return W9_PERCENT_STOCK;
  return value;
}

static void w9_scale_word(uint32_t address) {
  int64_t value = (int32_t)psx_mod_read_word(address);
  psx_mod_write_word(address,
                     (uint32_t)(int32_t)(value * s_percent / W9_PERCENT_STOCK));
}

/* The template is in the caller's stack frame, which the game keeps in the
 * scratchpad as well as in main RAM. */
static int w9_template_ok(uint32_t address) {
  uint32_t phys = address & 0x1FFFFFFFu;
  if ((address & 3u) != 0u)
    return 0;
  return phys <= 0x001FFFE0u || (phys >= 0x1F800000u && phys <= 0x1F8003E0u);
}

/* Pre-hook: scales the launch velocity, then declines so the original emitter
 * runs and charges its own cycles. */
static int w9_rigrange_emit(struct CPUState *cpu) {
  uint32_t particle;

  if (s_percent == W9_PERCENT_STOCK)
    return 0;
  if ((cpu->gpr[31] & 0x1FFFFFFFu) != (W9_BEAM_EMIT_RETURN & 0x1FFFFFFFu)) {
    psx_mod_counter_add("wild9.rigrange.bad_ra", 1u);
    return 0;
  }
  particle = cpu->gpr[5];
  if (!w9_template_ok(particle)) {
    psx_mod_counter_add("wild9.rigrange.bad_a1", 1u);
    return 0;
  }
  w9_scale_word(particle + W9_TEMPLATE_RADIAL_VEL);
  w9_scale_word(particle + W9_TEMPLATE_VERTICAL_VEL);
  return 0;
}

static void w9_rigrange_activate(void) {
  s_percent = w9_percent_option();
  /* Selector N picks values[N-1] of the game.toml site, one entry per step. */
  (void)psx_mod_set_immediate_selector(
      W9_RELEASE_IMM_GROUP,
      (uint32_t)((s_percent - W9_PERCENT_STOCK) / W9_PERCENT_STEP));
}

static void w9_rigrange_session_reset(void) { s_percent = W9_PERCENT_STOCK; }

PSX_MOD_CONSTRUCTOR(wild9_register_rigrange_plugin) {
  static const uint32_t emit_entry[1] = {W9_TRAIL_EMIT_ENTRY_WORD};

  (void)psx_mod_register_activation_plugin(PLUGIN_RIG_RANGE,
                                           w9_rigrange_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_RIG_RANGE,
                                              w9_rigrange_session_reset);
  (void)psx_mod_register_function_override(PLUGIN_RIG_RANGE,
                                           W9_TRAIL_EMIT_FUNC, w9_rigrange_emit,
                                           emit_entry, 1, 0);
}
