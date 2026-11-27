#include "mod_plugins.h"

/*
 * Presentation-only frame blending: the guest keeps its 59.94 Hz VBlank, so
 * physics, timers and audio are unchanged, and the OpenGL renderer blends
 * consecutive completed frames at the selected output rate. No motion vectors
 * or intermediate positions are computed. Motion-adaptive blending leaves less
 * double-image ghosting on fast sprites and projectiles than a linear
 * crossfade.
 */

#define WILD9_RATE_HOST_DISPLAY 0u

static void wild9_frame_rate_set(unsigned frames_per_second) {
  (void)psx_mod_set_frame_interpolation_blend(
      PSX_MOD_FRAME_INTERPOLATION_MOTION_ADAPTIVE);
  (void)psx_mod_set_frame_interpolation(frames_per_second);
}

static void wild9_frame_rate_60_activate(void) { wild9_frame_rate_set(60u); }

static void wild9_frame_rate_120_activate(void) { wild9_frame_rate_set(120u); }

static void wild9_frame_rate_144_activate(void) { wild9_frame_rate_set(144u); }

static void wild9_frame_rate_165_activate(void) { wild9_frame_rate_set(165u); }

static void wild9_frame_rate_display_activate(void) {
  wild9_frame_rate_set(WILD9_RATE_HOST_DISPLAY);
}

PSX_MOD_CONSTRUCTOR(wild9_register_frame_interpolation_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.framerate.60",
                                           wild9_frame_rate_60_activate);
  (void)psx_mod_register_activation_plugin("wild9.framerate.120",
                                           wild9_frame_rate_120_activate);
  (void)psx_mod_register_activation_plugin("wild9.framerate.144",
                                           wild9_frame_rate_144_activate);
  (void)psx_mod_register_activation_plugin("wild9.framerate.165",
                                           wild9_frame_rate_165_activate);
  (void)psx_mod_register_activation_plugin("wild9.framerate.uncapped",
                                           wild9_frame_rate_display_activate);
}
