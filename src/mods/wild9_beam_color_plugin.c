#include "cpu_state.h"
#include "gpu.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * The normal Rig beam (sprite 0x1E) takes its color from one CLUT, so a
 * pre-hook on beam_trail_draw uploads a recolored CLUT whenever VRAM does not
 * already hold it, such as after a level VRAM reload. The red and cable swing
 * beams have their own CLUTs and stay vanilla. The instruction sites in
 * game.toml recolor what the CLUT does not reach: the trail fade, two kinds of
 * sparks and, for the rainbow, the vertex colors of each trail segment.
 *
 * Cycle mode rebuilds the CLUT every logic frame. Rainbow mode builds a grey
 * CLUT once and tints each segment through its vertex colors, since one CLUT
 * is shared by the whole beam.
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

/* Advances once per 30 Hz logic frame and stops while paused. */
#define W9_LOGIC_FRAMES 0x8007755Cu
#define W9_LOGIC_HZ 30u

#define W9_BEAM_DRAW_FUNC 0x8004C8F0u
#define W9_BEAM_DRAW_ENTRY_WORD 0x27BDFF70u /* addiu sp,sp,-0x90 */

/* lw v0,0x14(sp), right after the trail's red fade step is stored. Vanilla
 * keeps red at full strength, which shifts the hue of a recolored tail. */
#define W9_FADE_SITE 0x8004CA78u
#define W9_FADE_SITE_WORD 0x8FA20014u
#define W9_FADE_RED_STEP 0x30u

/* sh v0,0x4(a3): last store of a segment's POLY_GT4, after its four vertex
 * colors. s0 counts segments from the Rig towards the tip; a3 = prim+0x2c. */
#define W9_SEGMENT_SITE 0x8004CE00u
#define W9_SEGMENT_SITE_WORD 0xA4E20004u
#define W9_PRIM_A3_OFFSET 0x2Cu
#define W9_PRIM_SIZE 0x34u

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
#define REG_A3 7
#define REG_S0 16
#define REG_S1 17
#define REG_S2 18
#define REG_S3 19
#define REG_SP 29

#define CLUT_ENTRIES 16u
#define CLUT_RAMP 11u
#define CLUT_STP 0x8000u
#define CLUT_FILLER 0x0707u /* vanilla entries 11..15, unused by the beam */

/* About 20 trail segments are visible, so about five bands show at once. */
#define BAND_SEGMENTS 4u

/* Time per color in tenths of a second. */
#define COLOR_TIME_MIN 3
#define COLOR_TIME_MAX 50
#define COLOR_TIME_DEFAULT 10

enum { MODE_STATIC, MODE_CYCLE, MODE_RAINBOW };

enum {
  COLOR_BLUE,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_VIOLET,
  COLOR_PURPLE,
  COLOR_CYAN,
  COLOR_PINK,
  COLOR_WHITE,
  COLOR_STATIC_COUNT,
  /* Animated modes only: a steady orange beam reads as the red beam. */
  COLOR_ORANGE = COLOR_STATIC_COUNT,
  COLOR_COUNT
};

/* Each channel mixes the vanilla main ramp (its blue) with its white core (its
 * red and green) by main_pct; blue {0,0,100},100,100 reproduces vanilla. */
typedef struct {
  int main_pct[3];
  int core_pct;
  int gain_pct;
} BeamMix;

typedef struct {
  const char *value;
  BeamMix mix;
} BeamColor;

static const BeamColor k_colors[COLOR_COUNT] = {
    [COLOR_BLUE] = {"blue", {{0, 0, 100}, 100, 100}},
    /* Darker and bluer than the cable swing beam so the two stay distinct. */
    [COLOR_GREEN] = {"green", {{0, 90, 60}, 60, 80}},
    [COLOR_YELLOW] = {"yellow", {{100, 85, 0}, 100, 100}},
    [COLOR_VIOLET] = {"violet", {{60, 0, 100}, 100, 100}},
    [COLOR_PURPLE] = {"purple", {{90, 0, 70}, 80, 100}},
    [COLOR_CYAN] = {"cyan", {{0, 90, 100}, 100, 100}},
    [COLOR_PINK] = {"pink", {{100, 30, 55}, 100, 100}},
    [COLOR_WHITE] = {"white", {{100, 100, 100}, 100, 100}},
    [COLOR_ORANGE] = {"orange", {{100, 45, 0}, 100, 100}},
};

/* Hue order used by the cycle and the rainbow. White has no hue. */
static const uint8_t k_animated[] = {COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN,
                                     COLOR_CYAN,   COLOR_BLUE,   COLOR_VIOLET,
                                     COLOR_PURPLE, COLOR_PINK};
#define ANIM_COUNT ((uint32_t)(sizeof(k_animated) / sizeof(k_animated[0])))

static const uint8_t k_ramp_main[CLUT_RAMP] = {0,  7,  15, 23, 25, 27,
                                               29, 31, 31, 31, 31};
static const uint8_t k_ramp_core[CLUT_RAMP] = {0,  1,  3,  6,  8, 10,
                                               12, 15, 17, 19, 21};

static int s_mode = MODE_STATIC;
static int s_color = COLOR_BLUE;
static int s_active;
static uint32_t s_frames_per_color = COLOR_TIME_DEFAULT * W9_LOGIC_HZ / 10u;
static uint32_t s_frame;
static uint32_t s_refreshed_frame = UINT32_MAX;
static int s_clut_ready;
static uint16_t s_clut[CLUT_ENTRIES];
static uint8_t s_spark_rgb[3];
static int8_t s_spark_step[3];
static uint8_t s_start_rgb[3];
static uint8_t s_tint[ANIM_COUNT][3];
static uint32_t s_vblank;
static uint32_t s_uploaded_vblank = UINT32_MAX;

static int w9_channel(const BeamMix *m, int ch, int main, int core) {
  const int w = m->main_pct[ch];
  int v = (w * main * 100 + (100 - w) * m->core_pct * core) * m->gain_pct;
  v = (v + 500000) / 1000000;
  return v < 0 ? 0 : v > 31 ? 31 : v;
}

static void w9_build_clut(const BeamMix *m) {
  uint32_t i;

  for (i = 0; i < CLUT_ENTRIES; ++i)
    s_clut[i] = i == 0 ? 0u : i < CLUT_RAMP ? CLUT_STP : CLUT_FILLER;
  for (i = 1; i < CLUT_RAMP; ++i) {
    const int r = w9_channel(m, 0, k_ramp_main[i], k_ramp_core[i]);
    const int g = w9_channel(m, 1, k_ramp_main[i], k_ramp_core[i]);
    const int b = w9_channel(m, 2, k_ramp_main[i], k_ramp_core[i]);
    s_clut[i] |= (uint16_t)(r | (g << 5) | (b << 10));
  }
}

static void w9_build_sparks(const BeamMix *m) {
  uint32_t i;

  /* Same model at vertex scale: blue gives the vanilla 0x40,0x40,0x80. */
  for (i = 0; i < 3; ++i) {
    const int w = m->main_pct[i];
    int v =
        (0x80 * (w * 200 + (100 - w) * m->core_pct) * m->gain_pct + 1000000) /
        2000000;
    v = v > 0x80 ? 0x80 : v;
    s_spark_rgb[i] = (uint8_t)v;
    /* Vanilla fades over 32 frames: 0x40 -> -2, 0x80 -> -4. */
    s_spark_step[i] = (int8_t)(-((v + 31) >> 5));
    /* Start sparks are the held-object spark at 3/8, vanilla 0x30 blue. */
    s_start_rgb[i] = (uint8_t)((v * 3 + 4) >> 3);
  }
}

/* Vertex tint over the grey CLUT, 0x80 = full. The core share is a quarter of
 * the spark model's so the bands keep a clear hue. */
static void w9_build_tint(const BeamMix *m, uint8_t tint[3]) {
  int i;

  for (i = 0; i < 3; ++i) {
    const int w = m->main_pct[i];
    int v =
        (0x80 * (w * 400 + (100 - w) * m->core_pct) * m->gain_pct + 2000000) /
        4000000;
    tint[i] = (uint8_t)(v > 0x80 ? 0x80 : v);
  }
}

static void w9_blend(const BeamMix *a, const BeamMix *b, int w256,
                     BeamMix *out) {
  int i;

  for (i = 0; i < 3; ++i)
    out->main_pct[i] =
        a->main_pct[i] + (b->main_pct[i] - a->main_pct[i]) * w256 / 256;
  out->core_pct = a->core_pct + (b->core_pct - a->core_pct) * w256 / 256;
  out->gain_pct = a->gain_pct + (b->gain_pct - a->gain_pct) * w256 / 256;
}

/* Animated color index of trail segment seg (0 at the Rig). Bands drift
 * towards the tip so each color stays on any point of the beam for
 * s_frames_per_color logic frames, entering in k_animated order. */
static uint32_t w9_band(uint32_t seg, uint32_t frame) {
  const uint32_t f = s_frames_per_color;
  const uint32_t unit = BAND_SEGMENTS * f;
  const uint32_t u =
      seg * f + ANIM_COUNT * unit - (frame % (ANIM_COUNT * f)) * BAND_SEGMENTS;
  return ANIM_COUNT - 1u - (u / unit) % ANIM_COUNT;
}

/* The cycle blends into the next color over the last quarter of each slot. */
static void w9_refresh(void) {
  const uint32_t f = s_frames_per_color;
  uint32_t pos, slot, fade_start;
  BeamMix mix;

  if (s_mode == MODE_STATIC)
    return;
  s_frame = psx_mod_read_word(W9_LOGIC_FRAMES);
  if (s_frame == s_refreshed_frame)
    return;
  s_refreshed_frame = s_frame;
  if (s_mode == MODE_RAINBOW) {
    w9_build_sparks(&k_colors[k_animated[w9_band(0, s_frame)]].mix);
    return;
  }
  pos = s_frame % (ANIM_COUNT * f);
  slot = pos / f;
  pos %= f;
  fade_start = f - f / 4u;
  w9_blend(&k_colors[k_animated[slot]].mix,
           &k_colors[k_animated[(slot + 1u) % ANIM_COUNT]].mix,
           pos < fade_start
               ? 0
               : (int)((pos - fade_start) * 256u / (f - fade_start)),
           &mix);
  w9_build_clut(&mix);
  w9_build_sparks(&mix);
}

static int w9_choice(const char *option, const char *const *values, int count,
                     int fallback) {
  char text[16] = "";
  int i;

  if (!psx_mod_option_value(PKG_BEAM_COLOR, FEATURE_BEAM_COLOR, option, text,
                            sizeof(text)))
    return fallback;
  for (i = 0; i < count; ++i)
    if (strcmp(text, values[i]) == 0)
      return i;
  return fallback;
}

static int w9_color_option(void) {
  const char *values[COLOR_STATIC_COUNT];
  int i;

  for (i = 0; i < COLOR_STATIC_COUNT; ++i)
    values[i] = k_colors[i].value;
  return w9_choice("color", values, COLOR_STATIC_COUNT, COLOR_BLUE);
}

static int w9_mode_option(void) {
  static const char *const values[] = {"off", "cycle", "rainbow"};

  return w9_choice("animation", values, 3, MODE_STATIC);
}

static int w9_color_time_option(void) {
  char text[16] = "";
  const char *p = text;
  int value = 0;

  if (!psx_mod_option_value(PKG_BEAM_COLOR, FEATURE_BEAM_COLOR, "color_time",
                            text, sizeof(text)))
    return COLOR_TIME_DEFAULT;
  if (*p < '0' || *p > '9')
    return COLOR_TIME_DEFAULT;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10 + (*p - '0');
    if (value > COLOR_TIME_MAX)
      return COLOR_TIME_DEFAULT;
  }
  if (*p != '\0' || value < COLOR_TIME_MIN)
    return COLOR_TIME_DEFAULT;
  return value;
}

static int w9_in_ram(uint32_t address, uint32_t bytes) {
  const uint32_t phys = address & 0x1FFFFFFFu;
  return (address >> 29) == 4u && phys >= 0x10000u && phys + bytes <= 0x200000u;
}

static int w9_is_beam_trail(uint32_t trail) {
  return w9_in_ram(trail, 0x40u) &&
         psx_mod_read_half(trail + W9_TRAIL_SPRITE) == W9_SPRITE_BEAM;
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

/* With HD textures on, every upload makes the render thread hand its GL context
 * to the emulation thread for the rest of the frame, and uploading each frame
 * dropped the game well below full speed. Uploads always land in
 * gpu_get_vram(), so an unchanged CLUT is skipped. */
static int w9_clut_in_vram(uint16_t clut) {
  const uint16_t *vram = gpu_get_vram();
  const uint32_t x = (clut & 0x3Fu) * 16u;
  const uint32_t y = (uint32_t)(clut >> 6) & 0x1FFu;

  return vram && memcmp(vram + y * 1024u + x, s_clut, sizeof(s_clut)) == 0;
}

/* LoadImage reads guest RAM, so the CLUT is staged over the trail data and
 * the original words are restored after the synchronous upload. */
static int w9_upload_clut(uint32_t stage, uint16_t clut) {
  uint32_t saved[CLUT_ENTRIES / 2u];
  uint32_t i;
  int ok;

  for (i = 0; i < CLUT_ENTRIES / 2u; ++i) {
    saved[i] = psx_mod_read_word(stage + 4u * i);
    psx_mod_write_word(stage + 4u * i,
                       (uint32_t)s_clut[2u * i] |
                           ((uint32_t)s_clut[2u * i + 1u] << 16));
  }
  ok = psx_mod_psyq_load_image((clut & 0x3Fu) * 16u, clut >> 6, CLUT_ENTRIES,
                               1u, stage);
  psx_mod_counter_add(
      ok ? "wild9.beamcolor.uploads" : "wild9.beamcolor.upload_failed", 1u);
  for (i = 0; i < CLUT_ENTRIES / 2u; ++i)
    psx_mod_write_word(stage + 4u * i, saved[i]);
  return ok;
}

/* Pre-hook on beam_trail_draw; always declines so the original draws. */
static int w9_beam_draw(struct CPUState *cpu) {
  const uint32_t trail = cpu->gpr[REG_A0];
  uint32_t rec, f, n;
  uint16_t clut;

  if (!s_active || !w9_is_beam_trail(trail))
    return 0;
  w9_refresh();
  if (s_uploaded_vblank == s_vblank)
    return 0;
  rec = w9_sprite_record(W9_SPRITE_BEAM);
  if (!rec || !(n = w9_frame_count(rec)))
    return 0;
  s_uploaded_vblank = s_vblank;
  s_clut_ready = 0;
  clut = w9_frame_clut(rec, 0);
  for (f = 1; f < n; ++f)
    if (w9_frame_clut(rec, f) != clut) {
      psx_mod_counter_add("wild9.beamcolor.mixed_clut", 1u);
      return 0;
    }
  if (w9_clut_in_vram(clut)) {
    s_clut_ready = 1;
    return 0;
  }
  if (!w9_clut_used_only_by_beam(clut)) {
    psx_mod_counter_add("wild9.beamcolor.shared_clut", 1u);
    return 0;
  }
  s_clut_ready = w9_upload_clut(trail, clut);
  return 0;
}

static void w9_fade_site(struct CPUState *cpu, uint32_t address) {
  int32_t step;
  (void)address;

  if (!s_active || !w9_is_beam_trail(cpu->gpr[REG_S1]))
    return;
  /* Same rounding as the game's green/blue step: -intensity / 32. */
  step = -(int32_t)cpu->gpr[REG_S3];
  if (step < 0)
    step += 31;
  psx_mod_write_word(cpu->gpr[REG_SP] + W9_FADE_RED_STEP,
                     (uint32_t)(step >> 5));
}

/* Each vertex keeps the game's brightness (its channels are equal once the
 * fade site has run) and takes the hue of its segment's band. */
static void w9_segment_site(struct CPUState *cpu, uint32_t address) {
  static const uint32_t k_vertex_rgb[4] = {0x04u, 0x10u, 0x1Cu, 0x28u};
  const uint32_t prim = cpu->gpr[REG_A3] - W9_PRIM_A3_OFFSET;
  const uint8_t *tint;
  uint32_t v, ch;
  (void)address;

  if (s_mode != MODE_RAINBOW || !s_clut_ready ||
      !w9_is_beam_trail(cpu->gpr[REG_S1]) || !w9_in_ram(prim, W9_PRIM_SIZE))
    return;
  tint = s_tint[w9_band(cpu->gpr[REG_S0], s_frame)];
  for (v = 0; v < 4u; ++v) {
    const uint32_t rgb = prim + k_vertex_rgb[v];
    uint32_t level = 0;
    for (ch = 0; ch < 3u; ++ch) {
      const uint32_t c = psx_mod_read_byte(rgb + ch);
      level = c > level ? c : level;
    }
    for (ch = 0; ch < 3u; ++ch)
      psx_mod_write_byte(rgb + ch, (uint8_t)((level * tint[ch] + 64u) >> 7));
  }
}

static void w9_spark_site(struct CPUState *cpu, uint32_t address) {
  const uint32_t spark = cpu->gpr[REG_A1];
  const uint32_t grabbed = cpu->gpr[REG_S3];
  uint32_t i;
  (void)address;

  if (!s_active || cpu->gpr[REG_S2] != W9_SPARK_TYPE_BEAM)
    return;
  /* s3 holds the grabbed node only when called from the held-object path. */
  if ((psx_mod_read_word(cpu->gpr[REG_SP] + W9_SPARK_SAVED_RA) & 0x1FFFFFFFu) !=
          (W9_SPARK_CALLER_RETURN & 0x1FFFFFFFu) ||
      !w9_in_ram(spark, 0x40u) || !w9_in_ram(grabbed, 0x200u)) {
    psx_mod_counter_add("wild9.beamcolor.spark_skipped", 1u);
    return;
  }
  if (psx_mod_read_half(grabbed + W9_GRABBED_FLAGS) & W9_GRABBED_SWING)
    return;
  w9_refresh();
  for (i = 0; i < 3; ++i)
    psx_mod_write_byte(spark + W9_SPARK_RGB + i, s_spark_rgb[i]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 3u, (uint8_t)s_spark_step[0]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 4u, (uint8_t)s_spark_step[1]);
  cpu->gpr[REG_V0] = (uint32_t)(int32_t)s_spark_step[2];
}

static void w9_start_site(struct CPUState *cpu, uint32_t address) {
  const uint32_t spark = cpu->gpr[REG_V1];
  (void)address;

  if (!s_active || !w9_in_ram(spark, 0x40u))
    return;
  /* Only the normal-beam branch stores 0x18,0x20 then 0x30. */
  if (cpu->gpr[REG_V0] != W9_START_B ||
      psx_mod_read_byte(spark + W9_SPARK_RGB) != W9_START_R ||
      psx_mod_read_byte(spark + W9_SPARK_RGB + 1u) != W9_START_G)
    return;
  w9_refresh();
  psx_mod_write_byte(spark + W9_SPARK_RGB, s_start_rgb[0]);
  psx_mod_write_byte(spark + W9_SPARK_RGB + 1u, s_start_rgb[1]);
  cpu->gpr[REG_V0] = s_start_rgb[2];
}

static void w9_beam_color_vblank(void) { ++s_vblank; }

static void w9_beam_color_activate(void) {
  uint32_t i;

  s_color = w9_color_option();
  s_mode = w9_mode_option();
  s_frames_per_color = (uint32_t)w9_color_time_option() * W9_LOGIC_HZ / 10u;
  s_active = s_mode != MODE_STATIC || s_color != COLOR_BLUE;
  s_uploaded_vblank = UINT32_MAX;
  s_refreshed_frame = UINT32_MAX;
  s_clut_ready = 0;
  if (s_mode == MODE_RAINBOW) {
    w9_build_clut(&k_colors[COLOR_WHITE].mix);
    for (i = 0; i < ANIM_COUNT; ++i)
      w9_build_tint(&k_colors[k_animated[i]].mix, s_tint[i]);
    w9_build_sparks(&k_colors[k_animated[0]].mix);
  } else if (s_mode == MODE_CYCLE) {
    w9_build_clut(&k_colors[k_animated[0]].mix);
    w9_build_sparks(&k_colors[k_animated[0]].mix);
  } else {
    w9_build_clut(&k_colors[s_color].mix);
    w9_build_sparks(&k_colors[s_color].mix);
  }
}

static void w9_beam_color_session_reset(void) {
  s_mode = MODE_STATIC;
  s_color = COLOR_BLUE;
  s_active = 0;
  s_uploaded_vblank = UINT32_MAX;
  s_refreshed_frame = UINT32_MAX;
  s_clut_ready = 0;
}

/* A restored state brings back its own VRAM, so check the CLUT again. */
static void w9_beam_color_savestate(void) {
  s_uploaded_vblank = UINT32_MAX;
  s_refreshed_frame = UINT32_MAX;
  s_clut_ready = 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_beam_color_plugin) {
  static const uint32_t draw_entry[1] = {W9_BEAM_DRAW_ENTRY_WORD};

  (void)psx_mod_register_activation_plugin(PLUGIN_BEAM_COLOR,
                                           w9_beam_color_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_BEAM_COLOR,
                                              w9_beam_color_session_reset);
  (void)psx_mod_register_savestate_plugin(PLUGIN_BEAM_COLOR,
                                          w9_beam_color_savestate);
  (void)psx_mod_register_vblank_plugin(PLUGIN_BEAM_COLOR, w9_beam_color_vblank);
  (void)psx_mod_register_function_override(PLUGIN_BEAM_COLOR, W9_BEAM_DRAW_FUNC,
                                           w9_beam_draw, draw_entry, 1, 0);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_FADE_SITE,
                                            W9_FADE_SITE_WORD, w9_fade_site);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_SEGMENT_SITE,
                                            W9_SEGMENT_SITE_WORD,
                                            w9_segment_site);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_SPARK_SITE,
                                            W9_SPARK_SITE_WORD, w9_spark_site);
  (void)psx_mod_register_instruction_plugin(PLUGIN_BEAM_COLOR, W9_START_SITE,
                                            W9_START_SITE_WORD, w9_start_site);
}
