#include "mod_plugins.h"
#include <stdlib.h>

/*
 * Wild 9 - Post-Process Display Gamma / Brightness Plugin
 *
 * Uses the public gl_renderer_set_post_gamma(float gamma) API of the
 * OpenGL renderer to adjust final display gamma at runtime without
 * modifying guest rendering state, textures, or generated/recompiled game code.
 *
 * The adjustment is applied in the final OpenGL presentation stage, after
 * the game frame has been rendered.
 *
 * Behavior:
 *   - gamma = 1.0 preserves the default presentation (vanilla).
 *   - Values greater than 1.0 lift shadows and mid-tones.
 *   - The adjustment applies only to game content and temporal interpolation passes.
 *   - Host UI elements, bezels, and previously composed frames are left untouched.
 *   - Changing the gamma value invalidates the presentation buffer so the new
 *     value becomes visible immediately, without requiring additional guest VRAM writes.
 *
 * This is useful for Wild 9 where the original presentation is excessively dark
 * (heavy distance darkening), while keeping the adjustment entirely in the
 * renderer's post-presentation stage.
 */

#define PKG_BRIGHTNESS          "wild9.enhancement.brightness"
#define FEATURE_BRIGHTNESS      "brightness"
#define OPTION_GAMMA            "gamma"
#define W9_DEFAULT_POST_GAMMA   1.30f

extern void gl_renderer_set_post_gamma(float gamma);

static float s_applied_gamma = 0.0f;

static float wild9_get_gamma_option(void)
{
    char text[32] = "";
    if (!psx_mod_option_value(PKG_BRIGHTNESS, FEATURE_BRIGHTNESS,
                              OPTION_GAMMA, text, sizeof(text)) || !text[0]) {
        return W9_DEFAULT_POST_GAMMA;
    }

    char* end = NULL;
    float gamma = strtof(text, &end);
    if (!end || end == text)
        return W9_DEFAULT_POST_GAMMA;

    /* Support percentage/scaled integer representations (e.g. 100..200 -> 1.0..2.0) */
    if (gamma >= 10.0f)
        gamma /= 100.0f;

    /* Clamp to safe bounds supported by gl_renderer_set_post_gamma */
    if (gamma < 0.5f)
        gamma = 0.5f;
    if (gamma > 3.0f)
        gamma = 3.0f;

    return gamma;
}

static void wild9_brightness_apply(void)
{
    float gamma = wild9_get_gamma_option();
    if (gamma != s_applied_gamma) {
        gl_renderer_set_post_gamma(gamma);
        s_applied_gamma = gamma;
    }
}

static void wild9_brightness_activate(void)
{
    wild9_brightness_apply();
}

static void wild9_brightness_vblank(void)
{
    /* Hot-reload gamma setting if changed in launcher/overlay during runtime */
    static unsigned s_check_interval = 0;
    if (++s_check_interval >= 30) {
        s_check_interval = 0;
        wild9_brightness_apply();
    }
}

PSX_MOD_CONSTRUCTOR(wild9_register_brightness_plugin)
{
    (void)psx_mod_register_activation_plugin(
        "wild9.brightness",
        wild9_brightness_activate);
    (void)psx_mod_register_vblank_plugin(
        "wild9.brightness",
        wild9_brightness_vblank);
}
