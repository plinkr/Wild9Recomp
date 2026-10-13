#include "mod_plugins.h"

#include <string.h>

#define PKG_LANGUAGE "wild9.localization.language"
#define FEATURE_LANGUAGE "language"
#define OPTION_LANGUAGE "language"
#define W9_LANGUAGE_ADDR 0x8007B3B4u

static uint32_t s_language = 2u;

static uint32_t language_index(const char *code) {
  if (strcmp(code, "it") == 0)
    return 0u;
  if (strcmp(code, "es") == 0)
    return 1u;
  if (strcmp(code, "de") == 0)
    return 3u;
  if (strcmp(code, "fr") == 0)
    return 4u;
  /* en and en-gb use the USA/UK English data */
  return 2u;
}

static void wild9_language_activate(void) {
  char code[16] = "en";
  if (!psx_mod_option_value(PKG_LANGUAGE, FEATURE_LANGUAGE, OPTION_LANGUAGE,
                            code, sizeof(code)))
    strcpy(code, "en");
  s_language = language_index(code);
}

static void wild9_language_vblank(void) {
  uint32_t current;

  if (!psx_mod_game_started())
    return;

  current = psx_mod_read_word(W9_LANGUAGE_ADDR);
  if (current == 2u && s_language != 2u)
    psx_mod_write_word(W9_LANGUAGE_ADDR, s_language);
}

PSX_MOD_CONSTRUCTOR(wild9_register_language_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.language",
                                           wild9_language_activate);
  (void)psx_mod_register_vblank_plugin("wild9.language", wild9_language_vblank);
}
