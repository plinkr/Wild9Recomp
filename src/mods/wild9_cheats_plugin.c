#include "mod_plugins.h"
#include <string.h>

#define PKG_CHEATS "wild9.gameplay.cheats"
#define FEATURE_CHEATS "cheats"

/* Memory addresses for General Cheats */
#define W9_LIVES_ADDR 0x8007B35Cu
#define W9_HEALTH_ADDR 0x80084466u
#define W9_MISSILES_ADDR 0x8007B444u
#define W9_PAD_ADDR 0x8007C788u
#define W9_Y_VEL_ADDR 0x80084492u

/* Memory addresses for Drench Level Codes */
#define W9_DRENCH_HEALTH_ADDR 0x80084786u
#define W9_DRENCH_Y_VEL_ADDR 0x800847B2u

#define W9_PAD_JUMP_MASK 0x0040u
#define W9_MOON_JUMP_VEL 0xFFF0u

static int s_initialized = 0;
static int s_infinite_lives = 0;
static int s_infinite_health = 0;
static int s_infinite_missiles = 0;
static int s_moon_jump = 0;
static int s_drench_infinite_health = 0;
static int s_drench_moon_jump = 0;

static int cheat_option_flag(const char *option_id) {
  char text[16] = "";
  return psx_mod_option_value(PKG_CHEATS, FEATURE_CHEATS, option_id, text,
                              sizeof(text)) &&
         strcmp(text, "true") == 0;
}

static void wild9_cheats_activate(void) {
  s_infinite_lives = cheat_option_flag("infinite_lives");
  s_infinite_health = cheat_option_flag("infinite_health");
  s_infinite_missiles = cheat_option_flag("infinite_missiles");
  s_moon_jump = cheat_option_flag("moon_jump");
  s_drench_infinite_health = cheat_option_flag("drench_infinite_health");
  s_drench_moon_jump = cheat_option_flag("drench_moon_jump");
  s_initialized = 1;
}

static void wild9_cheats_vblank(void) {
  if (!psx_mod_game_started())
    return;

  if (!s_initialized)
    wild9_cheats_activate();

  /* Infinite Lives: D007B35C 0002 -> 8007B35C 0003 */
  if (s_infinite_lives) {
    uint16_t lives = psx_mod_read_half(W9_LIVES_ADDR);
    if (lives == 0x0002u || lives == 0x0001u) {
      psx_mod_write_half(W9_LIVES_ADDR, 0x0003u);
    }
  }

  /* Infinite Health: D0084466 000F -> 80084466 0010 */
  if (s_infinite_health) {
    uint16_t health = psx_mod_read_half(W9_HEALTH_ADDR);
    if (health == 0x000Fu || (health > 0 && health < 0x0010u)) {
      psx_mod_write_half(W9_HEALTH_ADDR, 0x0010u);
    }
  }

  /* Infinite Missiles on Pick-Up: D007B444 0002 -> 8007B444 0003 */
  if (s_infinite_missiles) {
    uint16_t missiles = psx_mod_read_half(W9_MISSILES_ADDR);
    if (missiles == 0x0002u || missiles == 0x0001u) {
      psx_mod_write_half(W9_MISSILES_ADDR, 0x0003u);
    }
  }

  /* Moon Jump: D007C788 0040 -> 80084492 FFF0 */
  if (s_moon_jump) {
    uint16_t pad = psx_mod_read_half(W9_PAD_ADDR);
    if ((pad & W9_PAD_JUMP_MASK) != 0) {
      psx_mod_write_half(W9_Y_VEL_ADDR, W9_MOON_JUMP_VEL);
    }
  }

  /* Drench Level - Infinite Health: D0084786 0010 -> 80084786 0011 */
  if (s_drench_infinite_health) {
    uint16_t drench_health = psx_mod_read_half(W9_DRENCH_HEALTH_ADDR);
    if (drench_health == 0x0010u ||
        (drench_health > 0 && drench_health < 0x0011u)) {
      psx_mod_write_half(W9_DRENCH_HEALTH_ADDR, 0x0011u);
    }
  }

  /* Drench Level - Moon Jump: D007C788 0040 -> 800847B2 FFF0 */
  if (s_drench_moon_jump) {
    uint16_t pad = psx_mod_read_half(W9_PAD_ADDR);
    if ((pad & W9_PAD_JUMP_MASK) != 0) {
      psx_mod_write_half(W9_DRENCH_Y_VEL_ADDR, W9_MOON_JUMP_VEL);
    }
  }
}

PSX_MOD_CONSTRUCTOR(wild9_register_cheats_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.cheats",
                                           wild9_cheats_activate);
  (void)psx_mod_register_vblank_plugin("wild9.cheats", wild9_cheats_vblank);
}
