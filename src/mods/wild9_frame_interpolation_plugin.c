#include "mod_plugins.h"

/*
 * Wild 9 - Temporal Frame Blending Plugin
 *
 * Package: wild9.enhancement.frame-interpolation
 * Feature: frame-interpolation
 *
 * Preserves authentic guest execution cadence: guest VBlank frequency,
 * simulation physics, hardware timers, audio synchronization, and recompiled
 * game code remain unmodified at their original 59.94 Hz timing.
 *
 * The plugin configures the OpenGL presentation path to temporally blend
 * consecutive completed frames at the selected output rate (60, 120, 144,
 * 165 FPS, or host display refresh rate).
 *
 * Uses motion-adaptive temporal blending
 * (PSX_MOD_FRAME_INTERPOLATION_MOTION_ADAPTIVE) to reduce double-image
 * ghosting trails on fast sprites and projectiles compared to linear
 * crossfading.
 *
 * This implementation provides presentation-only temporal blending and does
 * not compute motion vectors or synthesize intermediate object positions.
 *
 * Calling psx_mod_set_frame_interpolation() engages the OpenGL renderer and
 * manages presentation pacing on the host presentation thread.
 */

#define WILD9_RATE_UNCAPPED 0u

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
  wild9_frame_rate_set(WILD9_RATE_UNCAPPED);
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
