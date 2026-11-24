#include "cpu_state.h"
#include "mod_plugins.h"

#include <string.h>

#define PKG_DEV_MENU "wild9.debug.dev-menu"
#define FEATURE_DEV_MENU "dev_menu"
#define PLUGIN_DEV_MENU "wild9.devmenu"

/* level_load loads the level in W9_NEXT_LEVEL_ADDR (script global G[1]),
 * copies it to W9_PREV_LEVEL_ADDR and resets it to the main menu. Nothing in
 * the shipped game requests level 0, the developers' level select. */
#define W9_LEVEL_LOAD_FUNC 0x8001576Cu
#define W9_NEXT_LEVEL_ADDR 0x8007B354u
#define W9_PREV_LEVEL_ADDR 0x8007B384u
#define W9_LEVEL_MAIN_MENU 0x29u
#define W9_LEVEL_DEV_MENU 0u
#define W9_LEVEL_NONE 0xFFFFFFFFu

static int s_open_every_time = 1;
static int s_startup_pending = 1;
static uint32_t s_previous_level = W9_LEVEL_NONE;

static void wild9_dev_menu_reset(void) {
  s_startup_pending = 1;
  s_previous_level = W9_LEVEL_NONE;
}

static void wild9_dev_menu_activate(void) {
  char open_on[16] = "always";

  if (!psx_mod_option_value(PKG_DEV_MENU, FEATURE_DEV_MENU, "open_on", open_on,
                            sizeof(open_on)))
    strcpy(open_on, "always");
  s_open_every_time = strcmp(open_on, "startup") != 0;
  wild9_dev_menu_reset();
}

static void wild9_dev_menu_savestate(void) {
  /* A restored state is already past boot. */
  s_startup_pending = 0;
  s_previous_level = psx_mod_read_word(W9_PREV_LEVEL_ADDR);
}

static void wild9_dev_menu_level_load(CPUState *cpu, uint32_t address) {
  uint32_t next;

  (void)address;
  if (!cpu || !psx_mod_game_started())
    return;

  next = psx_mod_read_word(W9_NEXT_LEVEL_ADDR);
  /* A main menu request right after the dev menu comes from its MAL MENU
   * entry or from a failed dev menu load; redirecting it would loop. */
  if (next == W9_LEVEL_MAIN_MENU && s_previous_level != W9_LEVEL_DEV_MENU &&
      (s_open_every_time || s_startup_pending)) {
    next = W9_LEVEL_DEV_MENU;
    psx_mod_write_word(W9_NEXT_LEVEL_ADDR, next);
  }
  if (next == W9_LEVEL_MAIN_MENU || next == W9_LEVEL_DEV_MENU)
    s_startup_pending = 0;
  s_previous_level = next;
}

PSX_MOD_CONSTRUCTOR(wild9_register_dev_menu_plugin) {
  (void)psx_mod_register_activation_plugin(PLUGIN_DEV_MENU,
                                           wild9_dev_menu_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_DEV_MENU,
                                              wild9_dev_menu_reset);
  (void)psx_mod_register_savestate_plugin(PLUGIN_DEV_MENU,
                                          wild9_dev_menu_savestate);
  (void)psx_mod_register_function_entry_plugin(
      PLUGIN_DEV_MENU, W9_LEVEL_LOAD_FUNC, wild9_dev_menu_level_load);
}
