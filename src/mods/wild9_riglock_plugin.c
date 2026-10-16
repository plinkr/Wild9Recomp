#include "cpu_state.h"
#include "mod_plugins.h"
#include "sio.h"

/*
 * Rig Overhead Hover (R2 + L2 / RT + LT)
 *
 * In vanilla Wild 9, carrying objects or enemies (such as Nitro in the Gulag
 * level) with the Rig is unwieldy because the carry spring physics and cable
 * swing were designed for swinging and throwing rather than stable transport.
 *
 * When both triggers are held, this mod anchors the held object directly above
 * the player so they can run and jump without dropping it.
 *
 * Why this requires coordination with engine carry routines:
 * The vanilla carry spring (FUN_8002b8cc) calculates corrective forces within
 * the Rig swing angle frame. Simply translating the prop overhead causes the
 * carry spring to violently fight the displacement with severe oscillations.
 * To maintain stability, we align the Rig swing angle and cable length target
 * with vanilla overhead values before the spring executes.
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

#define W9_P_SWING 0x160u

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
static uint32_t s_player = 0;
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

/* Different actor classes store their grab holder at different offsets
 * (enemies use +0x3D4, while metal boxes use +0x3D8 / +0x3B4 / +0x3B8). */
static int w9_grab_matches(uint32_t player, uint32_t holder) {
  if (!w9_ptr_ok(player) || !w9_ptr_ok(holder))
    return 0;
  return psx_mod_read_word(player + 0x3D4u) == holder ||
         psx_mod_read_word(player + 0x3D8u) == holder ||
         psx_mod_read_word(player + 0x3B4u) == holder ||
         psx_mod_read_word(player + 0x3B8u) == holder;
}

/* The frame hook does not receive the player actor in registers, so we resolve
 * and cache the player node here during carry entry. */
static void w9_cache_player(uint32_t actor, uint32_t holder) {
  uint32_t phys, player;
  if (!w9_ptr_ok(actor) || !w9_ptr_ok(holder))
    return;
  phys = psx_mod_read_word(actor + W9_OFF_ACTOR_PHYS);
  if (!w9_ptr_ok(phys))
    return;
  player = psx_mod_read_word(phys + W9_OFF_HOLDER_NODE);
  if (!w9_ptr_ok(player))
    return;
  if (!w9_grab_matches(player, holder))
    return;
  s_player = player;
}

/* Configure Rig cable length and swing angle before the carry spring calculates
 * displacement forces to prevent angular lag and severe spring oscillation.
 * Input state (0x8007C788) is deliberately untouched so trigger holding does
 * not block directional walking input. */
static void w9_riglock_carry_entry(CPUState *cpu, uint32_t address) {
  uint32_t holder;
  uint16_t pad;
  (void)address;
  if (!s_enabled || !cpu || !psx_mod_game_started())
    return;
  holder = psx_mod_read_word(W9_HELD_PTR);
  if (!holder)
    return;
  w9_cache_player(cpu->gpr[4], holder);

  pad = sio_get_pad_buttons();
  if ((pad & (W9_PAD_R2 | W9_PAD_L2)) == 0u) {
    psx_mod_write_word(W9_CABLE_LEN, W9_CABLE_UP);
    if (w9_ptr_ok(s_player)) {
      psx_mod_write_word(s_player + W9_P_SWING, 0u);
      psx_mod_write_word(s_player + 0x170u, 0u);
      psx_mod_write_word(s_player + 0x168u, 0x200u);
      psx_mod_write_word(s_player + 0x16cu, 0x40u);
    }
  }
}

/* Non-path nodes are integrated directly by FUN_800172a4 by adding the Rig's
 * radial channel to world coordinates, making the walk channel unstable for
 * them. Pinning them to the player's world offset keeps them stable while
 * moving. */
static void w9_hover_simple_horizontal(uint32_t held, uint32_t player) {
  int32_t px = (int32_t)psx_mod_read_word(player + W9_N_X);
  int32_t pz = (int32_t)psx_mod_read_word(player + W9_N_Z);
  if (!s_hover_on) {
    int32_t max = W9_HOVER_CAPTURE_MAX << 12;
    s_off_x = (int32_t)psx_mod_read_word(held + W9_N_X) - px;
    s_off_z = (int32_t)psx_mod_read_word(held + W9_N_Z) - pz;
    if (s_off_x > max)
      s_off_x = max;
    if (s_off_x < -max)
      s_off_x = -max;
    if (s_off_z > max)
      s_off_z = max;
    if (s_off_z < -max)
      s_off_z = -max;
  }
  psx_mod_write_word(held + W9_N_X, (uint32_t)(px + s_off_x));
  psx_mod_write_word(held + W9_N_Z, (uint32_t)(pz + s_off_z));
}

static void w9_riglock_frame(CPUState *cpu, uint32_t address) {
  uint32_t holder, held, player;
  uint16_t pad;
  int32_t target_y, cur_y, diff;
  (void)cpu;
  (void)address;
  if (!s_enabled || !psx_mod_game_started())
    return;

  holder = psx_mod_read_word(W9_HELD_PTR);
  if (!w9_ptr_ok(holder)) {
    s_player = 0;
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }
  held = psx_mod_read_word(holder + W9_OFF_HOLDER_NODE);
  if (!w9_ptr_ok(held)) {
    s_player = 0;
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

  player = s_player;
  if (!w9_ptr_ok(player) || !w9_grab_matches(player, holder)) {
    s_hover_on = 0;
    s_ramping = 0;
    return;
  }

  if (held != s_hover_node || !s_hover_on) {
    s_hover_node = held;
    s_hover_on = 1;
    s_ramping = 1;
  }

  psx_mod_write_word(player + W9_P_SWING, 0u);
  psx_mod_write_word(player + 0x170u, 0u);

  /* Clamping radial velocity on path nodes (enemies, metal boxes) ensures they
   * rise smoothly from their grab location instead of snapping jarringly. */
  if ((psx_mod_read_word(held + W9_N_FLAGS) & W9_N_TERRAIN) == 0u) {
    w9_hover_simple_horizontal(held, player);
  } else {
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

  /* Prevent retained momentum and accumulated render offsets from drifting
   * held props out of range while moving. */
  psx_mod_write_word(held + W9_N_IMP_RADIAL, 0u);
  psx_mod_write_word(held + W9_N_EXT_RADIAL, 0u);
  psx_mod_write_word(held + W9_N_IMP_HEIGHT, 0u);
  psx_mod_write_word(held + W9_N_EXT_HEIGHT, 0u);
  psx_mod_write_word(held + W9_N_IMP_LATERAL, 0u);
  psx_mod_write_word(held + W9_N_EXT_LATERAL, 0u);
  psx_mod_write_word(held + W9_N_OFF_X_IN, 0u);
  psx_mod_write_word(held + W9_N_OFF_Z_IN, 0u);

  /* Ramping vertical position initially allows the carry spring distance
   * measurement to adapt without physics glitching, after which the prop
   * tracks player vertical movement 1:1. */
  target_y = (int32_t)psx_mod_read_word(player + W9_N_Y) - w9_height_fixed();
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
  s_player = 0;
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
