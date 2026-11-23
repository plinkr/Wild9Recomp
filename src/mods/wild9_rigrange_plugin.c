#include "cpu_state.h"
#include "mod_plugins.h"
#include "wild9_rig_hold.h"

#include <stdint.h>

/*
 * The beam length is the launch velocity of its trail particles, so the
 * emitter hook scales that velocity. Its source, player+0x198, is also the
 * movement speed and cannot be scaled directly.
 *
 * The [mod_immediate] groups in game.toml adapt the hold code to a longer
 * beam. Group 1 scales the release distance, rounded up so far grabs are not
 * dropped at once. Group 2 scales the anchor rope cap, rounded down so the
 * rope never outgrows the beam. Group 3 moves the far edge of the hold band to
 * about the nearest screen edge, because scaling it left objects off screen.
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
#define W9_ROPE_MAX_IMM_GROUP 2u
#define W9_HOLD_FAR_IMM_GROUP 3u
/* Order of the group 3 values in game.toml. */
#define W9_HOLD_FAR_4_3 1u
#define W9_HOLD_FAR_WIDESCREEN 2u

#define W9_PERCENT_MIN 50
#define W9_PERCENT_STOCK 100
#define W9_PERCENT_MAX 500
#define W9_PERCENT_STEP 10

#define W9_IMM_FINE_MAX 200
#define W9_IMM_COARSE_STEP 60

static int s_percent = W9_PERCENT_STOCK;
static int s_widescreen;

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
  if (*p != '\0' || value < W9_PERCENT_MIN || value % W9_PERCENT_STEP != 0)
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

/* Selector N picks values[N-1] of a [mod_immediate] site: 1..10 are 110..200%
 * and 11..15 are 260..500%, so this mapping and the values lists in game.toml
 * must change together. */
static uint32_t w9_imm_selector(int percent, int round_up) {
  const int fine = (W9_IMM_FINE_MAX - W9_PERCENT_STOCK) / W9_PERCENT_STEP;
  const int over = percent - W9_IMM_FINE_MAX;

  if (over <= 0)
    return (uint32_t)((percent - W9_PERCENT_STOCK) / W9_PERCENT_STEP);
  return (uint32_t)(fine + (over + (round_up ? W9_IMM_COARSE_STEP - 1 : 0)) /
                               W9_IMM_COARSE_STEP);
}

/* Called from both activations, which may run in either order. */
static void w9_select_hold_far(void) {
  if (s_percent <= W9_PERCENT_STOCK)
    return;
  (void)psx_mod_set_immediate_selector(W9_HOLD_FAR_IMM_GROUP,
                                       s_widescreen ? W9_HOLD_FAR_WIDESCREEN
                                                    : W9_HOLD_FAR_4_3);
}

void wild9_rigrange_set_widescreen(int enabled) {
  s_widescreen = enabled != 0;
  w9_select_hold_far();
}

static void w9_rigrange_activate(void) {
  s_percent = w9_percent_option();
  if (s_percent <= W9_PERCENT_STOCK)
    return;
  (void)psx_mod_set_immediate_selector(W9_RELEASE_IMM_GROUP,
                                       w9_imm_selector(s_percent, 1));
  (void)psx_mod_set_immediate_selector(W9_ROPE_MAX_IMM_GROUP,
                                       w9_imm_selector(s_percent, 0));
  w9_select_hold_far();
}

static void w9_rigrange_session_reset(void) {
  s_percent = W9_PERCENT_STOCK;
  s_widescreen = 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_rigrange_plugin) {
  static const uint32_t emit_entry[1] = {W9_TRAIL_EMIT_ENTRY_WORD};

  (void)psx_mod_register_activation_plugin(PLUGIN_RIG_RANGE,
                                           w9_rigrange_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_RIG_RANGE,
                                              w9_rigrange_session_reset);
  (void)psx_mod_register_function_override(PLUGIN_RIG_RANGE, W9_TRAIL_EMIT_FUNC,
                                           w9_rigrange_emit, emit_entry, 1, 0);
}
