#include "cpu_state.h"
#include "mod_plugins.h"
#include "wild9_japanese_text_data.h"

#include <string.h>

/*
 * The USA executable only knows one-byte Latin glyph codes, so Japanese
 * cannot be enabled by changing its language value. Wildroid 9 (SCPS-10080)
 * runs the same script VM with 16-bit Shift-JIS strings on font 0x6A, so its
 * script is swapped in and font 0x6A is drawn here the way SCPS-10080 does.
 * Without the generated data or texture-bank support the English text stays.
 */

#define PKG_LANGUAGE "wild9.localization.language"
#define FEATURE_LANGUAGE "language"
#define OPTION_LANGUAGE "language"

#define W9_ARCHIVE_RECORD_FUNC 0x8001FD54u
#define W9_TEXT_RESET_FUNC 0x80025DF4u
#define W9_TEXT_QUEUE_FUNC 0x80025E3Cu
#define W9_TEXT_FLUSH_FUNC 0x80025CE8u
#define W9_TEXT_WIDTH_FUNC 0x800258ECu

#define W9_ARCHIVE_ENTRY 0x8007757Cu
#define W9_LOADING_LEVEL 0x8007B384u
#define W9_TEXT_OT 0x800779A4u
#define W9_TEXT_COLORS 0x8006E448u
#define W9_TEXT_TOP_COLOR 8u
#define W9_FRAME_BUFFER_INDEX 0x80077560u

#define W9_US_TEXT_ENTRY 0x7Bu
#define W9_TEXT_LEVEL4 4u

#define FOURCC(a, b, c, d)                                                     \
  ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 |            \
   (uint32_t)(d))
#define REC_ENT2 FOURCC('E', 'N', 'T', '2')
#define REC_VC_A FOURCC('V', 'C', '_', 'A')
#define REC_EXIT FOURCC('E', 'X', 'I', 'T')

#define W9_US_TEXT_VCA_SIZE 0x7DDCu
#define W9_US_TEXT_BYTES 0x7EE4u

#define JP_FONT 0x6Au
#define JP_ADVANCE 13

#define BANK_ID 0x4A39u
#define BANK_W WILD9_JA_FONT_W
#define BANK_H WILD9_JA_FONT_H

#define QUEUE_MAX 64u
#define QUEUE_UNITS 2048u
#define GLYPHS_PER_FRAME 512u
#define GT3_BYTES 40u
#define GLYPH_BYTES (2u * GT3_BYTES)

typedef Wild9JaGlyph Glyph;

typedef struct {
  int32_t x, y;
  uint32_t color;
  uint32_t start, count;
} QueuedText;

static int s_enabled;
static int s_bank_defined;
static const uint8_t *s_text_main;
static uint32_t s_text_main_size;
static const uint8_t *s_text_level4;
static uint32_t s_text_level4_size;
static const uint16_t *s_bank_pixels;
static uint32_t s_arena[2];

static QueuedText s_queue[QUEUE_MAX];
static uint32_t s_queue_count;
static uint16_t s_units[QUEUE_UNITS];
static uint32_t s_units_used;

#ifdef WILD9_HAVE_JAPANESE_TEXT
static int bind_data(void) {
  s_text_main = wild9_ja_text_main;
  s_text_main_size = wild9_ja_text_main_size;
  s_text_level4 = wild9_ja_text_level4;
  s_text_level4_size = wild9_ja_text_level4_size;
  s_bank_pixels = wild9_ja_font_pixels;
  return s_text_main_size <= W9_US_TEXT_BYTES &&
         s_text_level4_size <= W9_US_TEXT_BYTES && wild9_ja_glyph_count != 0u;
}

static const Glyph *glyph_for_code(uint32_t code) {
  uint32_t lo = 0, hi = wild9_ja_glyph_count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2u;
    if (wild9_ja_glyphs[mid].code < code)
      lo = mid + 1u;
    else
      hi = mid;
  }
  return lo < wild9_ja_glyph_count && wild9_ja_glyphs[lo].code == code
             ? &wild9_ja_glyphs[lo]
             : NULL;
}
#else
static int bind_data(void) { return 0; }
static const Glyph *glyph_for_code(uint32_t code) {
  (void)code;
  return NULL;
}
#endif

static void wild9_japanese_text_activate(void) {
  char code[16] = "";
  s_enabled = 0;
  s_queue_count = s_units_used = 0;
  if (!psx_mod_option_value(PKG_LANGUAGE, FEATURE_LANGUAGE, OPTION_LANGUAGE,
                            code, sizeof(code)) ||
      strcmp(code, "ja") != 0)
    return;
  if (!bind_data()) {
    psx_mod_counter_add("wild9.ja.no_data", 1u);
    return;
  }
  if (!s_arena[0]) {
    s_arena[0] =
        psx_mod_alloc_texture_packet_memory(GLYPHS_PER_FRAME * GLYPH_BYTES, 4u);
    s_arena[1] =
        psx_mod_alloc_texture_packet_memory(GLYPHS_PER_FRAME * GLYPH_BYTES, 4u);
  }
  if (!s_arena[0] || !s_arena[1]) {
    psx_mod_counter_add("wild9.ja.no_arena", 1u);
    return;
  }
  s_enabled = 1;
}

/* Japanese text only replaces English when it can also be drawn. */
static int renderer_ready(void) {
  if (!s_enabled || !psx_mod_texture_banks_supported())
    return 0;
  if (!s_bank_defined)
    s_bank_defined =
        psx_mod_define_texture_bank(BANK_ID, BANK_W, BANK_H, s_bank_pixels);
  return s_bank_defined;
}

/* Replacing the package before it is parsed lets the USA engine run the
 * Japanese script and layout without further patches. */
static void wild9_japanese_text_record(CPUState *cpu, uint32_t address) {
  const uint32_t header = cpu->gpr[4], data = cpu->gpr[5];
  const uint32_t buffer = data - 12u;
  const uint8_t *package;
  uint32_t size;
  (void)address;
  if (psx_mod_read_word(header) != REC_ENT2 ||
      psx_mod_read_word(W9_ARCHIVE_ENTRY) != W9_US_TEXT_ENTRY ||
      psx_mod_read_word(buffer) != REC_ENT2 ||
      psx_mod_read_word(buffer + 0x18u) != REC_VC_A ||
      psx_mod_read_word(buffer + 0x20u) != W9_US_TEXT_VCA_SIZE ||
      psx_mod_read_word(buffer + W9_US_TEXT_BYTES - 12u) != REC_EXIT ||
      !renderer_ready())
    return;
  if (psx_mod_read_word(W9_LOADING_LEVEL) == W9_TEXT_LEVEL4) {
    package = s_text_level4;
    size = s_text_level4_size;
  } else {
    package = s_text_main;
    size = s_text_main_size;
  }
  if (psx_mod_host_write_ram(buffer, package, size))
    psx_mod_counter_add("wild9.ja.package", 1u);
}

static void wild9_japanese_text_reset(CPUState *cpu, uint32_t address) {
  (void)cpu;
  (void)address;
  s_queue_count = s_units_used = 0;
}

static int wild9_japanese_text_queue(CPUState *cpu, uint32_t address) {
  const uint32_t font = psx_mod_read_word(cpu->gpr[29] + 0x10u);
  uint32_t str = cpu->gpr[4];
  QueuedText *q;
  (void)address;
  if (font != JP_FONT || !s_enabled)
    return 0;
  if (cpu->gpr[7] >= 9u || s_queue_count >= QUEUE_MAX)
    return 1;
  q = &s_queue[s_queue_count];
  q->x = (int32_t)cpu->gpr[5];
  q->y = (int32_t)cpu->gpr[6];
  q->color = cpu->gpr[7];
  q->start = s_units_used;
  q->count = 0;
  for (;;) {
    const uint16_t unit = psx_mod_read_half(str);
    if (unit == 0u || s_units_used >= QUEUE_UNITS)
      break;
    s_units[s_units_used++] = unit;
    q->count++;
    str += 2u;
  }
  s_queue_count++;
  psx_mod_counter_add("wild9.ja.queued", 1u);
  return 1;
}

static int wild9_japanese_text_width(CPUState *cpu, uint32_t address) {
  uint32_t str = cpu->gpr[4], width = 0;
  (void)address;
  if (cpu->gpr[5] != JP_FONT || !s_enabled)
    return 0;
  while (psx_mod_read_half(str) != 0u && width < 0x10000u) {
    width += JP_ADVANCE;
    str += 2u;
  }
  cpu->gpr[2] = width;
  return 1;
}

static uint32_t xy(int32_t x, int32_t y) {
  return ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
}

static void put_gt3(uint32_t p, uint32_t next_slot, uint32_t c0, uint32_t c1,
                    uint32_t c2, uint32_t xy0, uint32_t xy1, uint32_t xy2,
                    uint32_t uv0, uint32_t uv1, uint32_t uv2) {
  const uint32_t tpage = 2u << 7;
  psx_mod_write_word(p + 4u, 0x34000000u | (c0 & 0xFFFFFFu));
  psx_mod_write_word(p + 8u, xy0);
  psx_mod_write_word(p + 12u, uv0);
  psx_mod_write_word(p + 16u, (BANK_ID & 0xFFu) << 24 | (c1 & 0xFFFFFFu));
  psx_mod_write_word(p + 20u, xy1);
  psx_mod_write_word(p + 24u, tpage << 16 | uv1);
  psx_mod_write_word(p + 28u, (BANK_ID >> 8) << 24 | (c2 & 0xFFFFFFu));
  psx_mod_write_word(p + 32u, xy2);
  psx_mod_write_word(p + 36u, uv2);
  /* Same linking as the stock emitter, so draw order matches English text. */
  psx_mod_write_word(p,
                     0x09000000u | (psx_mod_read_word(next_slot) & 0xFFFFFFu));
  psx_mod_write_word(next_slot, p & 0xFFFFFFu);
}

static void wild9_japanese_text_flush(CPUState *cpu, uint32_t address) {
  const uint32_t ot = psx_mod_read_word(W9_TEXT_OT);
  const uint32_t arena = s_arena[psx_mod_read_word(W9_FRAME_BUFFER_INDEX) & 1u];
  uint32_t used = 0;
  (void)cpu;
  (void)address;
  if (!s_queue_count)
    return;
  if (!ot || !arena || !renderer_ready()) {
    s_queue_count = s_units_used = 0;
    return;
  }
  for (uint32_t i = 0; i < s_queue_count; ++i) {
    const QueuedText *q = &s_queue[i];
    const uint32_t colors = W9_TEXT_COLORS + q->color * 16u;
    const uint32_t c0 = psx_mod_read_word(colors),
                   c1 = psx_mod_read_word(colors + 4u);
    const uint32_t c2 = psx_mod_read_word(colors + 8u),
                   c3 = psx_mod_read_word(colors + 12u);
    const uint32_t slot = q->color == W9_TEXT_TOP_COLOR ? ot : ot + 4u;
    int32_t x = q->x;
    for (uint32_t n = 0; n < q->count; ++n) {
      const uint16_t unit = s_units[q->start + n];
      /* Strings store the lead byte first, so the halfword is byte swapped. */
      const Glyph *g =
          glyph_for_code((uint32_t)(unit & 0xFFu) << 8 | unit >> 8);
      if (g && used < GLYPHS_PER_FRAME) {
        const uint32_t p = arena + used * GLYPH_BYTES;
        const int32_t x0 = x + g->x0, x1 = x + g->x1;
        const int32_t y0 = q->y + g->y0, y1 = q->y + g->y1;
        const uint32_t uv0 = (uint32_t)g->v << 8 | g->u;
        const uint32_t uv1 = (uint32_t)g->v << 8 | (uint32_t)(g->u + g->w);
        const uint32_t uv2 = (uint32_t)(g->v + g->h) << 8 | g->u;
        const uint32_t uv3 =
            (uint32_t)(g->v + g->h) << 8 | (uint32_t)(g->u + g->w);
        put_gt3(p + GT3_BYTES, slot, c1, c2, c3, xy(x1, y0), xy(x0, y1),
                xy(x1, y1), uv1, uv2, uv3);
        put_gt3(p, slot, c0, c1, c2, xy(x0, y0), xy(x1, y0), xy(x0, y1), uv0,
                uv1, uv2);
        used++;
      }
      x += JP_ADVANCE;
    }
  }
  if (used)
    psx_mod_counter_add("wild9.ja.glyphs", used);
  s_queue_count = s_units_used = 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_japanese_text_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.japanese_text",
                                           wild9_japanese_text_activate);
  (void)psx_mod_register_function_entry_plugin("wild9.japanese_text",
                                               W9_ARCHIVE_RECORD_FUNC,
                                               wild9_japanese_text_record);
  (void)psx_mod_register_function_entry_plugin(
      "wild9.japanese_text", W9_TEXT_RESET_FUNC, wild9_japanese_text_reset);
  (void)psx_mod_register_function_entry_plugin(
      "wild9.japanese_text", W9_TEXT_FLUSH_FUNC, wild9_japanese_text_flush);
  (void)psx_mod_register_function_filter_plugin(
      "wild9.japanese_text", W9_TEXT_QUEUE_FUNC, wild9_japanese_text_queue);
  (void)psx_mod_register_function_filter_plugin(
      "wild9.japanese_text", W9_TEXT_WIDTH_FUNC, wild9_japanese_text_width);
}
