#include "cpu_state.h"
#include "mod_plugins.h"

#define PKG_WS "wild9.enhancement.widescreen"

#define W9_CLIP_BOUND_ADDR 0x80081264u

#define W9_SCREEN_W_ADDR 0x80081258u
#define W9_LAYER_TABLE_ADDR 0x8007C8C0u

/* The tiled layer routine also draws the title and loading images, so it
 * cannot be widened unconditionally like the other layer types. */
#define W9_TILED_LAYER_FUNC 0x80013774u
#define W9_TILED_LAYER_ENTRY_WORD 0x27BDFFC8u
#define W9_STATIC_SCREEN_RA_A 0x800154BCu
#define W9_STATIC_SCREEN_RA_B 0x80015698u

#define W9_TILE_COUNT_A_ADDR 0x80013848u
#define W9_TILE_COUNT_A_WORD 0x24750001u
#define W9_NARROW_BRANCH_ADDR 0x8001385Cu
#define W9_NARROW_BRANCH_WORD 0x10400003u
#define W9_NARROW_BRANCH_SKIP 0x10000003u
#define W9_TILE_INDEX_ADDR 0x80013A24u
#define W9_TILE_INDEX_WORD 0x24590001u
#define W9_GRID_START_A_ADDR 0x80013B58u
#define W9_GRID_START_A_WORD 0x2522FFE0u
#define W9_GRID_START_B_ADDR 0x80013B98u
#define W9_GRID_START_B_WORD 0x2522FFE0u

#define W9_TILE_COUNT_B_ADDR 0x800149ECu
#define W9_TILE_COUNT_B_WORD 0x24730001u

static uint32_t s_clip_base;
static uint32_t s_clip_applied;
static uint32_t s_tile_count_b;

static uint32_t w9_tile_cols(int32_t margin) {
  if (margin <= 0)
    return 1;

  return 1u + 2u * (uint32_t)((margin + 31) / 32);
}

static uint32_t w9_left_tiles(int32_t margin) {
  if (margin <= 0)
    return 0;

  return (uint32_t)((margin + 30) / 32);
}

static void w9_patch_clip(int32_t margin) {
  uint32_t word = psx_mod_read_word(W9_CLIP_BOUND_ADDR);

  if (word != s_clip_applied)
    s_clip_base = word;

  if (s_clip_base == 0)
    return;

  uint32_t max_y = s_clip_base >> 16;
  uint32_t max_x = s_clip_base & 0xFFFFu;

  if (margin > 0 && max_x <= 0xFFFFu - (uint32_t)margin)
    max_x += (uint32_t)margin;

  uint32_t want = (max_y << 16) | max_x;

  if (word != want)
    psx_mod_write_word(W9_CLIP_BOUND_ADDR, want);

  s_clip_applied = want;
}

static void w9_patch_imm16(uint32_t addr, uint32_t vanilla, uint32_t *applied,
                           uint32_t imm) {
  uint32_t word = psx_mod_read_word(addr);

  if (word != vanilla && word != *applied)
    return;

  uint32_t want = (vanilla & 0xFFFF0000u) | (imm & 0xFFFFu);

  if (word != want)
    psx_mod_write_code_word(addr, want);

  *applied = want;
}

/* Compare only opcode and target register: the immediate and the branch
 * source register are the parts this plugin rewrites. */
static void w9_set_code(uint32_t addr, uint32_t vanilla, uint32_t want) {
  uint32_t word = psx_mod_read_word(addr);

  if ((word & 0xFC1F0000u) != (vanilla & 0xFC1F0000u))
    return;

  if (word != want)
    psx_mod_write_code_word(addr, want);
}

enum { W9_LAYER_VANILLA = 0, W9_LAYER_FIT = 1, W9_LAYER_WIDE = 2 };

static void w9_tiled_layer_code(int mode, int32_t margin) {
  uint32_t cols = 1u, index = 1u, grid = 0xFFE0u;
  uint32_t branch = W9_NARROW_BRANCH_WORD;

  if (mode == W9_LAYER_FIT) {
    /* Stock code leaves a wrapped column at X -32, hidden at 4:3 but visible
     * in the left band, so start at X 0 with exactly one screen of tiles. */
    cols = 0u;
    index = 0u;
    grid = 0u;
  } else if (mode == W9_LAYER_WIDE) {
    uint32_t left = w9_left_tiles(margin);

    cols = w9_tile_cols(margin);
    /* Without moving the tile index by the same amount as the screen X, the
     * whole layer would shift left instead of extending into the band. */
    index = 1u + left;
    grid = (uint32_t)(-32 - 32 * (int32_t)left) & 0xFFFFu;
    /* The extra columns can exceed the layer width; the stock no-wrap clamp
     * would then draw only the layer's own columns and leave a gap. */
    branch = W9_NARROW_BRANCH_SKIP;
  }

  w9_set_code(W9_TILE_COUNT_A_ADDR, W9_TILE_COUNT_A_WORD,
              (W9_TILE_COUNT_A_WORD & 0xFFFF0000u) | cols);
  w9_set_code(W9_NARROW_BRANCH_ADDR, W9_NARROW_BRANCH_WORD, branch);
  w9_set_code(W9_TILE_INDEX_ADDR, W9_TILE_INDEX_WORD,
              (W9_TILE_INDEX_WORD & 0xFFFF0000u) | index);
  w9_set_code(W9_GRID_START_A_ADDR, W9_GRID_START_A_WORD,
              (W9_GRID_START_A_WORD & 0xFFFF0000u) | grid);
  w9_set_code(W9_GRID_START_B_ADDR, W9_GRID_START_B_WORD,
              (W9_GRID_START_B_WORD & 0xFFFF0000u) | grid);
}

static int w9_static_screen_call(uint32_t ra) {
  ra &= 0x1FFFFFFFu;
  return ra == (W9_STATIC_SCREEN_RA_A & 0x1FFFFFFFu) ||
         ra == (W9_STATIC_SCREEN_RA_B & 0x1FFFFFFFu);
}

/* Decided per call rather than per frame because a frame can draw layers
 * that need different treatment. Title and loading images were composed for
 * 4:3, so they stay centred with clear bands; gameplay layers wrap and can
 * fill the bands. */
static int w9_tiled_layer_prehook(struct CPUState *cpu) {
  int32_t margin = psx_mod_widescreen_x_margin();
  int mode = W9_LAYER_VANILLA;

  if (margin > 0) {
    uint32_t slot = cpu->gpr[4];
    uint32_t id = psx_mod_read_word(slot);
    uint32_t header =
        id < 0x100u ? psx_mod_read_word(W9_LAYER_TABLE_ADDR + id * 8u) : 0u;

    if (header != 0) {
      int32_t layer_cols = (int32_t)psx_mod_read_half(header + 0x10u);
      int32_t screen_w = (int16_t)psx_mod_read_half(W9_SCREEN_W_ADDR);
      int32_t vanilla_cols = ((screen_w + 0x1F) >> 5) + 1;
      uint32_t scroll_x = psx_mod_read_word(slot + 0x14u);

      if (w9_static_screen_call(cpu->gpr[31])) {
        if (layer_cols * 32 == screen_w && (scroll_x & 0x1Fu) == 0)
          mode = W9_LAYER_FIT;
      } else if (layer_cols + 1 >= vanilla_cols) {
        mode = W9_LAYER_WIDE;
      }
    }
  }

  psx_mod_counter_add(mode == W9_LAYER_WIDE  ? "wild9.widescreen.layer_wide"
                      : mode == W9_LAYER_FIT ? "wild9.widescreen.layer_fit"
                                             : "wild9.widescreen.layer_stock",
                      1);
  w9_tiled_layer_code(mode, margin);
  return 0;
}

static void wild9_widescreen_vblank(void) {
  if (!psx_mod_game_started())
    return;

  int32_t margin = psx_mod_widescreen_x_margin();

  w9_patch_clip(margin);

  uint32_t tile_imm = margin > 0 ? w9_tile_cols(margin) : 1u;
  w9_patch_imm16(W9_TILE_COUNT_B_ADDR, W9_TILE_COUNT_B_WORD, &s_tile_count_b,
                 tile_imm);
}

static void wild9_widescreen_activate(void) {
  psx_mod_set_fixed_display_aspect(16, 9);
}

PSX_MOD_CONSTRUCTOR(wild9_register_widescreen_plugin) {
  static const uint32_t tiled_layer_entry[1] = {W9_TILED_LAYER_ENTRY_WORD};

  (void)psx_mod_register_activation_plugin("wild9.widescreen",
                                           wild9_widescreen_activate);
  (void)psx_mod_register_vblank_plugin("wild9.widescreen",
                                       wild9_widescreen_vblank);
  /* The hook only selects code and always declines, so it charges no guest
   * cycles. */
  (void)psx_mod_register_function_override(
      "wild9.widescreen:tiled_layer", W9_TILED_LAYER_FUNC,
      w9_tiled_layer_prehook, tiled_layer_entry, 1, 0);
}
