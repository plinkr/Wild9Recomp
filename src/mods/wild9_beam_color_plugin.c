#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * The normal Rig beam (sprite 0x1E) takes its color from one CLUT, so a
 * pre-hook on beam_trail_draw re-uploads a recolored CLUT once per vblank,
 * after any level VRAM reload. The red and cable swing beams have their own
 * CLUTs and stay vanilla. The instruction sites in game.toml recolor what the
 * CLUT does not reach: the trail fade and two kinds of sparks.
 */

#define PKG_BEAM_COLOR "wild9.enhancement.beam-color"
#define FEATURE_BEAM_COLOR "beam_color"
#define PLUGIN_BEAM_COLOR "wild9.beamcolor"

#define W9_SPRITE_TABLE 0x8007C8C0u
#define W9_SPRITE_COUNT 0xA0u
#define W9_SPRITE_REC_FRAMES 0x04u
#define W9_SPRITE_REC_FRAME0 0x14u
#define W9_SPRITE_FRAME_SIZE 0x1Cu
#define W9_SPRITE_FRAME_CLUT 0x02u
#define W9_SPRITE_MAX_FRAMES 64u
#define W9_SPRITE_BEAM 0x1Eu

#define W9_TRAIL_SPRITE 0x02u

#define W9_BEAM_DRAW_FUNC 0x8004C8F0u
#define W9_BEAM_DRAW_ENTRY_WORD 0x27BDFF70u /* addiu sp,sp,-0x90 */

/* lw v0,0x14(sp), right after the trail's red fade step is stored. Vanilla
 * keeps red at full strength, which shifts the hue of a recolored tail. */
#define W9_FADE_SITE 0x8004CA78u
#define W9_FADE_SITE_WORD 0x8FA20014u
#define W9_FADE_RED_STEP 0x30u

/* sb v0,0x11(a1): stores the blue fade of a new held-object spark. */
#define W9_SPARK_SITE 0x8004D04Cu
#define W9_SPARK_SITE_WORD 0xA0A20011u
#define W9_SPARK_TYPE_BEAM 6u
#define W9_SPARK_RGB 0x0Cu
#define W9_SPARK_SAVED_RA 0x3Cu
#define W9_SPARK_CALLER_RETURN 0x8002AE28u /* after jal at 0x8002AE20 */
#define W9_GRABBED_FLAGS 0x1CAu
#define W9_GRABBED_SWING 0x10u

/* sb v0,0xe(v1) in rig_beam_emit_start_spark: blue store shared by the
 * normal, swing and red beam start spark colors. */
#define W9_START_SITE 0x8004D56Cu
#define W9_START_SITE_WORD 0xA062000Eu
#define W9_START_R 0x18u
#define W9_START_G 0x20u
#define W9_START_B 0x30u

#define REG_V0 2
#define REG_V1 3
#define REG_A0 4
#define REG_A1 5
#define REG_S1 17
#define REG_S2 18
#define REG_S3 19
#define REG_SP 29

#define CLUT_ENTRIES 16u
#define CLUT_RAMP 11u
#define CLUT_STP 0x8000u
#define CLUT_FILLER 0x0707u /* vanilla entries 11..15, unused by the beam */

enum { COLOR_BLUE, COLOR_GREEN, COLOR_YELLOW, COLOR_VIOLET, COLOR_PURPLE };

/* Each channel mixes the vanilla main ramp (its blue) with its white core (its
 * red and green) by main_pct; blue {0,0,100},100,100 reproduces vanilla. */
typedef struct {
  const char *value;
  int main_pct[3];
  int core_pct;
  int gain_pct;
} BeamColor;

static const BeamColor k_colors[] = {
    [COLOR_BLUE] = {"blue", {0, 0, 100}, 100, 100},
    /* Darker and bluer than the cable swing beam so the two stay distinct. */
    [COLOR_GREEN] = {"green", {0, 90, 60}, 60, 80},
    [COLOR_YELLOW] = {"yellow", {100, 85, 0}, 100, 100},
    [COLOR_VIOLET] = {"violet", {60, 0, 100}, 100, 100},
    [COLOR_PURPLE] = {"purple", {90, 0, 70}, 80, 100},
};

static const uint8_t k_ramp_main[CLUT_RAMP] = {0,  7,  15, 23, 25, 27,
                                               29, 31, 31, 31, 31};
static const uint8_t k_ramp_core[CLUT_RAMP] = {0,  1,  3,  6,  8, 10,
                                               12, 15, 17, 19, 21};

static int s_color = COLOR_BLUE;
static uint16_t s_clut[CLUT_ENTRIES];
static uint8_t s_spark_rgb[3];
static int8_t s_spark_step[3];
static uint8_t s_start_rgb[3];
static uint32_t s_vblank;
static uint32_t s_uploaded_vblank = UINT32_MAX;

static int w9_channel(const BeamColor *c, int ch, int main, int core) {
  const int w = c->main_pct[ch];
  int v = (w * main * 100 + (100 - w) * c->core_pct * core) * c->gain_pct;
  v = (v + 500000) / 1000000;
  return v < 0 ? 0 : v > 31 ? 31 : v;
}

static void w9_build_color(int color) {
  const BeamColor *c = &k_colors[color];
  uint32_t i;

  for (i = 0; i < CLUT_ENTRIES; ++i)
    s_clut[i] = i == 0 ? 0u : i < CLUT_RAMP ? CLUT_STP : CLUT_FILLER;
  for (i = 1; i < CLUT_RAMP; ++i) {
    const int r = w9_channel(c, 0, k_ramp_main[i], k_ramp_core[i]);
    const int g = w9_channel(c, 1, k_ramp_main[i], k_ramp_core[i]);
    const int b = w9_channel(c, 2, k_ramp_main[i], k_ramp_core[i]);
    s_clut[i] |= (uint16_t)(r | (g << 5) | (b << 10));
  }
  /* Same model at vertex scale: blue gives the vanilla 0x40,0x40,0x80. */
  for (i = 0; i < 3; ++i) {
    const int w = c->main_pct[i];
    int v =
        (0x80 * (w * 200 + (100 - w) * c->core_pct) * c->gain_pct + 1000000) /
        2000000;
    v = v > 0x80 ? 0x80 : v;
    s_spark_rgb[i] = (uint8_t)v;
    /* Vanilla fades over 32 frames: 0x40 -> -2, 0x80 -> -4. */
    s_spark_step[i] = (int8_t)(-((v + 31) >> 5));
    /* Start sparks are the held-object spark at 3/8, vanilla 0x30 blue. */
    s_start_rgb[i] = (uint8_t)((v * 3 + 4) >> 3);
  }
}

static int w9_color_option(void) {
  char text[16] = "";
  int i;

  if (!psx_mod_option_value(PKG_BEAM_COLOR, FEATURE_BEAM_COLOR, "color", text,
                            sizeof(text)))
    return COLOR_BLUE;
  for (i = 0; i < (int)(sizeof(k_colors) / sizeof(k_colors[0])); ++i)
    if (strcmp(text, k_colors[i].value) == 0)
      return i;
  return COLOR_BLUE;
}

static int w9_in_ram(uint32_t address, uint32_t bytes) {
  const uint32_t phys = address & 0x1FFFFFFFu;
  return (address >> 29) == 4u && phys >= 0x10000u && phys + bytes <= 0x200000u;
}

static uint32_t w9_sprite_record(uint32_t id) {
  const uint32_t rec = psx_mod_read_word(W9_SPRITE_TABLE + id * 8u);
  return w9_in_ram(rec, W9_SPRITE_REC_FRAME0) ? rec : 0u;
}

static uint32_t w9_frame_count(uint32_t rec) {
  const uint32_t n = psx_mod_read_word(rec + W9_SPRITE_REC_FRAMES);
  return n > W9_SPRITE_MAX_FRAMES ? W9_SPRITE_MAX_FRAMES : n;
}

static uint16_t w9_frame_clut(uint32_t rec, uint32_t frame) {
  return psx_mod_read_half(rec + W9_SPRITE_REC_FRAME0 +
                           frame * W9_SPRITE_FRAME_SIZE + W9_SPRITE_FRAME_CLUT);
}

static int w9_clut_used_only_by_beam(uint16_t clut) {
  uint32_t id, f;

  for (id = 0; id < W9_SPRITE_COUNT; ++id) {
    uint32_t rec;
    if (id == W9_SPRITE_BEAM || !(rec = w9_sprite_record(id)))
      continue;
    for (f = 0; f < w9_frame_count(rec); ++f)
      if (w9_frame_clut(rec, f) == clut)
        return 0;
  }
  return 1;
}

/* LoadImage reads guest RAM, so the CLUT is staged over the trail data and
 * the original words are restored after the synchronous upload. */
static void w9_upload_clut(uint32_t stage, uint16_t clut) {
  uint32_t saved[CLUT_ENTRIES / 2u];
  uint32_t i;

  for (i = 0; i < CLUT_ENTRIES / 2u; ++i) {
    saved[i] = psx_mod_read_word(stage + 4u * i);
    psx_mod_write_word(stage + 4u * i,
                       (uint32_t)s_clut[2u * i] |
                           ((uint32_t)s_clut[2u * i + 1u] << 16));
  }
  if (!psx_mod_psyq_load_image((clut & 0x3Fu) * 16u, clut >> 6, CLUT_ENTRIES,
                               1u, stage))
    psx_mod_counter_add("wild9.beamcolor.upload_failed", 1u);
  for (i = 0; i < CLUT_ENTRIES / 2u; ++i)
    psx_mod_write_word(stage + 4u * i, saved[i]);
}

/* Pre-hook on beam_trail_draw; always declines so the original draws. */
static int w9_beam_draw(struct CPUState *cpu) {
  const uint32_t trail = cpu->gpr[REG_A0];
  uint32_t rec, f, n;
  uint16_t clut;

  if (s_color == COLOR_BLUE || s_uploaded_vblank == s_vblank)
    return 0;
  if (!w9_in_ram(trail, 0x40u) ||
      psx_mod_read_half(trail + W9_TRAIL_SPRITE) != W9_SPRITE_BEAM)
    return 0;
  rec = w9_sprite_record(W9_SPRITE_BEAM);
  if (!rec || !(n = w9_frame_count(rec)))
    return 0;
  s_uploaded_vblank = s_vblank;
  clut = w9_frame_clut(rec, 0);
  for (f = 1; f < n; ++f)
    if (w9_frame_clut(rec, f) != clut) {
      psx_mod_counter_add("wild9.beamcolor.mixed_clut", 1u);
      return 0;
    }
  if (!w9_clut_used_only_by_beam(clut)) {
    psx_mod_counter_add("wild9.beamcolor.shared_clut", 1u);
    return 0;
  }
  w9_upload_clut(trail, clut);
  return 0;
}

static void w9_fade_site(struct CPUState *cpu, uint32_t address) {
  const uint32_t trail = cpu->gpr[REG_S1];
  int32_t step;
  (void)address;

  if (s_color == COLOR_BLUE || !w9_in_ram(trail, 0x40u) ||
      psx_mod_read_half(trail + W9_TRAIL_SPRITE) != W9_SPRITE_BEAM)
    return;
  /* Same rounding as the game's green/blue step: -intensity / 32. */
  step = -(int32_t)cpu->gpr[REG_S3];
  if (step < 0)
    step += 31;
  psx_mod_write_word(cpu->gpr[REG_SP] + W9_FADE_RED_STEP,
                     (uint32_t)(step >> 5));
}

static void w9_spark_site(struct CPUState *cpu, uint32_t address) {
  const uint32_t spark = cpu->gpr[REG_A1];
  const uint32_t grabbed = cpu->gpr[REG_S3];
  uint32_t i;
  (void)address;

  if (s_color == COLOR_BLUE || cpu->gpr[REG_S2] != W9_SPARK_TYPE_BEAM)
    return;
  /* s3 holds the grabbed node only when called from the held-object path. */
  if ((psx_mod_read_word(cpu->gpr[REG_SP] + W9_SPARK_SAVED_RA) &
       0x1FFFFFFFu) != (W9_SPARK_CALLER_RETURN & 0x1FFFFFFFu) ||
      !w9_in_ram(spark, 0x40u) || !w9_in_ram(grabbed, 0x200u)) {
    psx_mod_counter_add("wild9.beamcolor.spark_skipped", 1u);
    return;
  }
  if (psx_mod_read_half(grabbed + W9_GRABBED_FLAGS) & W9_GRABBED_SWING)
    return;
  for (i = 0; i < 3; ++i)
    psx_mod_write_byte(spark + W9_SPARK_RGB + i, s_spark_rgb[i]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 3u, (uint8_t)s_spark_step[0]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 4u, (uint8_t)s_spark_step[1]);
  cpu->gpr[REG_V0] = (uint32_t)(int32_t)s_spark_step[2];
}

static void w9_start_site(struct CPUState *cpu, uint32_t address) {
  const uint32_t spark = cpu->gpr[REG_V1];
  (void)address;

  if (s_color == COLOR_BLUE || !w9_in_ram(spark, 0x40u))
    return;
  /* Only the normal-beam branch stores 0x18,0x20 then 0x30. */
  if (cpu->gpr[REG_V0] != W9_START_B ||
      psx_mod_read_byte(spark + W9_SPARK_RGB) != W9_START_R ||
      psx_mod_read_byte(spark + W9_SPARK_RGB + 1u) != W9_START_G)
    return;
  psx_mod_write_byte(spark + W9_SPARK_RGB, s_start_rgb[0]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 1u, s_start_rgb[1]);
  cpu->gpr[REG_V0] = s_start_rgb[2];
}

static void w9_beam_color_vblank(void) { ++s_vblank; }

static void w9_beam_color_activate(void) {
  s_color = w9_color_option();
  s_uploaded_vblank = UINT32_MAX;
  w9_build_color(s_color);
}

static void w9_beam_color_session_reset(void) {
  s_color = COLOR_BLUE;
  s_uploaded_vblank = UINT32_MAX;
}

/* A restored state brings back its own VRAM, so upload again. */
static void w9_beam_color_savestate(void) { s_uploaded_vblank = UINT32_MAX; }

PSX_MOD_CONSTRUCTOR(wild9_register_beam_color_plugin) {
  static const uint32_t draw_entry[1] = {W9_BEAM_DRAW_ENTRY_WORD};

  (void)psx_mod_register_activation_plugin(PLUGIN_BEAM_COLOR,
                                           w9_beam_color_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_BEAM_COLOR,
                                              w9_beam_color_session_reset);
  (void)psx_mod_register_savestate_plugin(PLUGIN_BEAM_COLOR,
                                          w9_beam_color_savestate);
  (void)psx_mod_register_vblank_plugin(PLUGIN_BEAM_COLOR,
                                       w9_beam_color_vblank);
  (void)psx_mod_register_function_override(PLUGIN_BEAM_COLOR, W9_BEAM_DRAW_FUNC,
                                           w9_beam_draw, draw_entry, 1, 0);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_FADE_SITE,
                                            W9_FADE_SITE_WORD, w9_fade_site);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_SPARK_SITE,
                                            W9_SPARK_SITE_WORD, w9_spark_site);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_START_SITE,
                                            W9_START_SITE_WORD, w9_start_site);
}
