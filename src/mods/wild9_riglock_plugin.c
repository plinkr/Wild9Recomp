#include "cpu_state.h"
#include "mod_plugins.h"
#include "sio.h"

/*
 * While R2 and L2 are both held, the object or enemy the Rig carries stays
 * directly above Wex, so he can run and jump with it (Nitro in the Gulag, for
 * example). Releasing either trigger restores the stock carry physics, which
 * were built for swinging and throwing rather than transport.
 *
 * Moving the object overhead is not enough on its own: the carry spring
 * (0x8002B8CC) computes its correction in the Rig's swing angle frame and
 * fights the displacement with heavy oscillation. Its entry hook first sets
 * the swing angle and cable length target to the vanilla overhead values.
 */

#define PKG_RIG_LOCK "wild9.gameplay.riglock"
#define FEATURE_RIG_LOCK "rig_lock"

#define W9_HELD_PTR 0x80077864u
#define W9_SIN_TABLE 0x8006FE08u

#define W9_OFF_ACTOR_PHYS 0x44u
#define W9_OFF_HOLDER_NODE 0x10u
#define W9_OFF_GRAB 0x3D4u

#define W9_N_X 0x2ECu
#define W9_N_Y 0x2F0u
#define W9_N_Z 0x2F4u
#define W9_N_FLAGS 0x190u
#define W9_N_TERRAIN 0x80u
#define W9_N_ROTATION 0x300u

#define W9_N_HEIGHT_DRIVE 0x1FCu
#define W9_N_HEIGHT_TARGET 0x200u

#define W9_N_RADIAL_DRIVE 0x1DCu
#define W9_N_RADIAL_TARGET 0x1E0u

#define W9_PAD_L2 0x0100u
#define W9_PAD_R2 0x0200u

#define W9_HOOK_CARRY 0x8002B8CCu
#define W9_HOOK_FRAME 0x80020BA4u

#define W9_INPUT_WORD 0x8007C788u
#define W9_INPUT_UP 0x1u

#define W9_CABLE_LEN 0x80077AF8u
#define W9_CABLE_UP 0x007D0000u

/* Each frame rig_carry_spring moves W9_RIG_SWING, the offset from the cable
 * angle, towards W9_RIG_SWING_TARGET by at most W9_RIG_SWING_RATE and within
 * W9_RIG_SWING_LIMIT. Angles use 4096 units per turn; 0 points straight up. */
#define W9_RIG_SWING 0x160u
#define W9_RIG_SWING_LIMIT 0x168u
#define W9_RIG_SWING_RATE 0x16Cu
#define W9_RIG_SWING_TARGET 0x170u
#define W9_SWING_TARGET_UP 0u
/* rig_carry_spring uses these while a D-pad direction steers the swing. */
#define W9_SWING_LIMIT_STEER 0x200u
#define W9_SWING_RATE_STEER 0x40u

#define W9_N_IMP_RADIAL 0x1E8u
#define W9_N_EXT_RADIAL 0x1ECu
#define W9_N_IMP_HEIGHT 0x208u
#define W9_N_EXT_HEIGHT 0x20Cu
#define W9_N_IMP_LATERAL 0x228u
#define W9_N_EXT_LATERAL 0x22Cu
#define W9_N_OFF_X_IN 0x1F4u
#define W9_N_OFF_Z_IN 0x234u

#define W9_DEFAULT_HEIGHT_UNITS 3000
#define W9_OPTION_MIN 0
#define W9_OPTION_MAX 12000

#define W9_RAMP_SPEED (200 << 12)
#define W9_RAMP_SNAP (100 << 12)
#define W9_HOVER_CAPTURE_MAX 3400

static int s_enabled = 0;
static int s_height_units = W9_DEFAULT_HEIGHT_UNITS;
static uint32_t s_rig_node = 0;
static int s_hover_on = 0;
static int s_ramping = 0;
static uint32_t s_hover_node = 0;
static int32_t s_off_x = 0;
static int32_t s_off_z = 0;

static int32_t w9_height_fixed(void) { return (int32_t)s_height_units << 12; }

static int w9_ptr_ok(uint32_t address) {
  address &= 0x1FFFFFFFu;
  return address >= 0x1000u && address <= 0x1FFFFCu && (address & 3u) == 0u;
}

static int32_t w9_sin12(int index) {
  uint32_t e =
      psx_mod_read_word(W9_SIN_TABLE + ((uint32_t)(index & 0xFFF) * 4u));
  return (int32_t)(int16_t)(e >> 16);
}

static int32_t w9_cos12(int index) {
  uint32_t e =
      psx_mod_read_word(W9_SIN_TABLE + ((uint32_t)(index & 0xFFF) * 4u));
  return (int32_t)(int16_t)(e & 0xFFFFu);
}

static int w9_option(const char *id, int fallback) {
  char text[24] = "";
  long value = 0;
  int negative = 0;
  const char *p = text;
  if (!psx_mod_option_value(PKG_RIG_LOCK, FEATURE_RIG_LOCK, id, text,
                            sizeof(text)))
    return fallback;
  if (*p == '-') {
    negative = 1;
    ++p;
  }
  if (*p < '0' || *p > '9')
    return fallback;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10 + (*p - '0');
    if (value > 0x7FFFFFFFL)
      return fallback;
  }
  if (*p != '\0' || value < W9_OPTION_MIN || value > W9_OPTION_MAX)
    return fallback;
  return (int)(negative ? -value : value);
}

/* The grab holder lives at a different offset per held class: +0x3D4 for
 * enemies, +0x3D8, +0x3B4 or +0x3B8 for metal boxes. */
static int w9_grab_matches(uint32_t rig, uint32_t holder) {
  if (!w9_ptr_ok(rig) || !w9_ptr_ok(holder))
    return 0;
  return psx_mod_read_word(rig + 0x3D4u) == holder ||
         psx_mod_read_word(rig + 0x3D8u) == holder ||
         psx_mod_read_word(rig + 0x3B4u) == holder ||
         psx_mod_read_word(rig + 0x3B8u) == holder;
}

/* The frame hook has no register pointing to the Rig, so the carry hook caches
 * the Rig grabber node, the one rig_carry_spring works on, for it. */
static void w9_cache_rig_node(uint32_t actor, uint32_t holder) {
  uint32_t phys, rig;
  if (!w9_ptr_ok(actor) || !w9_ptr_ok(holder))
    return;
  phys = psx_mod_read_word(actor + W9_OFF_ACTOR_PHYS);
  if (!w9_ptr_ok(phys))
    return;
  rig = psx_mod_read_word(phys + W9_OFF_HOLDER_NODE);
  if (!w9_ptr_ok(rig))
    return;
  if (!w9_grab_matches(rig, holder))
    return;
  s_rig_node = rig;
}

/* Runs before the carry spring computes its forces, so setting the cable length
 * and swing angle here avoids angular lag and oscillation. The input word at
 * 0x8007C788 is left alone so holding the triggers does not block walking. */
static void w9_riglock_carry_entry(CPUState *cpu, uint32_t address) {
  uint32_t holder;
  uint16_t pad;
  (void)address;
  if (!s_enabled || !cpu || !psx_mod_game_started())
    return;
  holder = psx_mod_read_word(W9_HELD_PTR);
  if (!holder)
    return;
  w9_cache_rig_node(cpu->gpr[4], holder);

  pad = sio_get_pad_buttons();
  if ((pad & (W9_PAD_R2 | W9_PAD_L2)) == 0u) {
    psx_mod_write_word(W9_CABLE_LEN, W9_CABLE_UP);
    if (w9_ptr_ok(s_rig_node)) {
      psx_mod_write_word(s_rig_node + W9_RIG_SWING, 0u);
      psx_mod_write_word(s_rig_node + W9_RIG_SWING_TARGET, W9_SWING_TARGET_UP);
      psx_mod_write_word(s_rig_node + W9_RIG_SWING_LIMIT, W9_SWING_LIMIT_STEER);
      psx_mod_write_word(s_rig_node + W9_RIG_SWING_RATE, W9_SWING_RATE_STEER);
    }
  }
}

/* 0x800172A4 moves nodes that are not on a path by adding the Rig's radial
 * channel straight to their world position, so they drift while Wex walks.
 * Pinning them to a fixed offset from the Rig keeps them steady. */
static void w9_hover_simple_horizontal(uint32_t held, uint32_t rig) {
  int32_t rig_x = (int32_t)psx_mod_read_word(rig + W9_N_X);
  int32_t rig_z = (int32_t)psx_mod_read_word(rig + W9_N_Z);
  if (!s_hover_on) {
    int32_t max = W9_HOVER_CAPTURE_MAX << 12;
    s_off_x = (int32_t)psx_mod_read_word(held + W9_N_X) - rig_x;
    s_off_z = (int32_t)psx_mod_read_word(held + W9_N_Z) - rig_z;
    if (s_off_x > max)
      s_off_x = max;
    if (s_off_x < -max)
      s_off_x = -max;
    if (s_off_z > max)
      s_off_z = max;
    if (s_off_z < -max)
      s_off_z = -max;
  }
  psx_mod_write_word(held + W9_N_X, (uint32_t)(rig_x + s_off_x));
  psx_mod_write_word(held + W9_N_Z, (uint32_t)(rig_z + s_off_z));
}

static void w9_riglock_frame(CPUState *cpu, uint32_t address) {
  uint32_t holder, held, rig;
  uint16_t pad;
  int32_t target_y, cur_y, diff;
  (void)cpu;
  (void)address;
  if (!s_enabled || !psx_mod_game_started())
    return;

  holder = psx_mod_read_word(W9_HELD_PTR);
  if (!w9_ptr_ok(holder)) {
    s_rig_node = 0;
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }
  held = psx_mod_read_word(holder + W9_OFF_HOLDER_NODE);
  if (!w9_ptr_ok(held)) {
    s_rig_node = 0;
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }

  pad = sio_get_pad_buttons();
  if ((pad & W9_PAD_R2) != 0u || (pad & W9_PAD_L2) != 0u) {
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }

  rig = s_rig_node;
  if (!w9_ptr_ok(rig) || !w9_grab_matches(rig, holder)) {
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }

  if (held != s_hover_node || !s_hover_on) {
    s_hover_node = held;
    s_hover_on = 1;
    s_ramping = 1;
  }

  psx_mod_write_word(rig + W9_RIG_SWING, 0u);
  psx_mod_write_word(rig + W9_RIG_SWING_TARGET, W9_SWING_TARGET_UP);

  if ((psx_mod_read_word(held + W9_N_FLAGS) & W9_N_TERRAIN) == 0u) {
    w9_hover_simple_horizontal(held, rig);
  } else {
    /* Clamping the radial drive of path nodes, such as enemies and metal
     * boxes, makes them rise from the grab point instead of snapping. */
    int32_t rad = (int32_t)psx_mod_read_word(held + W9_N_RADIAL_DRIVE);
    int32_t rad_max = 250 << 12;
    if (rad > rad_max) {
      psx_mod_write_word(held + W9_N_RADIAL_DRIVE, (uint32_t)rad_max);
      psx_mod_write_word(held + W9_N_RADIAL_TARGET, (uint32_t)rad_max);
    } else if (rad < -rad_max) {
      psx_mod_write_word(held + W9_N_RADIAL_DRIVE, (uint32_t)(-rad_max));
      psx_mod_write_word(held + W9_N_RADIAL_TARGET, (uint32_t)(-rad_max));
    }
  }

  /* Retained momentum and accumulated offsets would otherwise carry the object
   * out of range while Wex moves. */
  psx_mod_write_word(held + W9_N_IMP_RADIAL, 0u);
  psx_mod_write_word(held + W9_N_EXT_RADIAL, 0u);
  psx_mod_write_word(held + W9_N_IMP_HEIGHT, 0u);
  psx_mod_write_word(held + W9_N_EXT_HEIGHT, 0u);
  psx_mod_write_word(held + W9_N_IMP_LATERAL, 0u);
  psx_mod_write_word(held + W9_N_EXT_LATERAL, 0u);
  psx_mod_write_word(held + W9_N_OFF_X_IN, 0u);
  psx_mod_write_word(held + W9_N_OFF_Z_IN, 0u);

  /* The object first rises at W9_RAMP_SPEED so the carry spring's distance
   * measurement can adapt, then follows the Rig's height exactly. */
  target_y = (int32_t)psx_mod_read_word(rig + W9_N_Y) - w9_height_fixed();
  cur_y = (int32_t)psx_mod_read_word(held + W9_N_Y);
  diff = target_y - cur_y;

  if (s_ramping) {
    if (diff < -W9_RAMP_SPEED) {
      cur_y -= W9_RAMP_SPEED;
    } else if (diff > W9_RAMP_SPEED) {
      cur_y += W9_RAMP_SPEED;
    } else {
      cur_y = target_y;
      s_ramping = 0;
    }
  } else {
    cur_y = target_y;
  }

  psx_mod_write_word(held + W9_N_Y, (uint32_t)cur_y);
  psx_mod_write_word(held + W9_N_HEIGHT_DRIVE, 0u);
  psx_mod_write_word(held + W9_N_HEIGHT_TARGET, 0u);
}

static void w9_riglock_activate(void) {
  s_enabled = 1;
  s_rig_node = 0;
  s_hover_on = 0;
  s_ramping = 0;
  s_hover_node = 0;
  s_height_units = w9_option("height_offset", W9_DEFAULT_HEIGHT_UNITS);
}

PSX_MOD_CONSTRUCTOR(wild9_register_riglock_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.riglock",
                                           w9_riglock_activate);
  (void)psx_mod_register_function_entry_plugin("wild9.riglock", W9_HOOK_CARRY,
                                               w9_riglock_carry_entry);
  (void)psx_mod_register_function_entry_plugin("wild9.riglock", W9_HOOK_FRAME,
                                               w9_riglock_frame);
}
