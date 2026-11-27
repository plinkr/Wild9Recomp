#include "mod_plugins.h"
#include <stdlib.h>

/*
 * Wild 9 is very dark, mostly from its heavy distance darkening. This lifts
 * shadows and mid-tones through the OpenGL renderer's presentation gamma, so
 * guest rendering state, textures and the recompiled code stay untouched and
 * host UI and bezels are not affected. A gamma of 1.0 is vanilla.
 */

#define PKG_BRIGHTNESS "wild9.enhancement.brightness"
#define FEATURE_BRIGHTNESS "brightness"
#define OPTION_GAMMA "gamma"
#define W9_DEFAULT_POST_GAMMA 1.30f

extern void gl_renderer_set_post_gamma(float gamma);

static float s_applied_gamma = 0.0f;

static float wild9_get_gamma_option(void) {
  char text[32] = "";
  if (!psx_mod_option_value(PKG_BRIGHTNESS, FEATURE_BRIGHTNESS, OPTION_GAMMA,
                            text, sizeof(text)) ||
      !text[0]) {
    return W9_DEFAULT_POST_GAMMA;
  }

  char *end = NULL;
  float gamma = strtof(text, &end);
  if (!end || end == text)
    return W9_DEFAULT_POST_GAMMA;

  /* Percentages are accepted too: 130 means 1.30. */
  if (gamma >= 10.0f)
    gamma /= 100.0f;

  /* Same range gl_renderer_set_post_gamma clamps to. */
  if (gamma < 0.5f)
    gamma = 0.5f;
  if (gamma > 3.0f)
    gamma = 3.0f;

  return gamma;
}

static void wild9_brightness_apply(void) {
  float gamma = wild9_get_gamma_option();
  if (gamma != s_applied_gamma) {
    gl_renderer_set_post_gamma(gamma);
    s_applied_gamma = gamma;
  }
}

static void wild9_brightness_activate(void) { wild9_brightness_apply(); }

static void wild9_brightness_vblank(void) {
  /* Re-reads the option about twice a second so changes made while the game
   * runs take effect. */
  static unsigned s_check_interval = 0;
  if (++s_check_interval >= 30) {
    s_check_interval = 0;
    wild9_brightness_apply();
  }
}

PSX_MOD_CONSTRUCTOR(wild9_register_brightness_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.brightness",
                                           wild9_brightness_activate);
  (void)psx_mod_register_vblank_plugin("wild9.brightness",
                                       wild9_brightness_vblank);
}
