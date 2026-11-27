#ifndef WILD9_JAPANESE_TEXT_DATA_H
#define WILD9_JAPANESE_TEXT_DATA_H

#include <stdint.h>

/* Implemented by generated/wild9_japanese_text.c, which is game data and
 * therefore ships only in the generated/ bundle, never in git. */

#define WILD9_JA_FONT_W 256u
#define WILD9_JA_FONT_H 160u

typedef struct {
  uint16_t code;
  int8_t x0, y0, x1, y1;
  uint8_t u, v, w, h;
} Wild9JaGlyph;

extern const uint8_t wild9_ja_text_main[];
extern const uint32_t wild9_ja_text_main_size;
extern const uint8_t wild9_ja_text_level4[];
extern const uint32_t wild9_ja_text_level4_size;
extern const uint16_t wild9_ja_font_pixels[WILD9_JA_FONT_W * WILD9_JA_FONT_H];
/* Sorted by code, for binary search. */
extern const Wild9JaGlyph wild9_ja_glyphs[];
extern const uint32_t wild9_ja_glyph_count;

#endif
