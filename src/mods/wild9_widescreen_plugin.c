#include "mod_plugins.h"

#define PKG_WS "wild9.enhancement.widescreen"

#define W9_CLIP_BOUND_ADDR      0x80081264u

#define W9_TILE_COUNT_A_ADDR    0x80013848u
#define W9_TILE_COUNT_A_WORD    0x24750001u
#define W9_TILE_COUNT_B_ADDR    0x800149ECu
#define W9_TILE_COUNT_B_WORD    0x24730001u

#define W9_GRID_START_A_ADDR    0x80013B58u
#define W9_GRID_START_A_WORD    0x2522FFE0u
#define W9_GRID_START_B_ADDR    0x80013B98u
#define W9_GRID_START_B_WORD    0x2522FFE0u

#define W9_INNER_CULL_ADDR      0x8001A748u
#define W9_INNER_CULL_WORD      0x36318000u
#define W9_INNER_CULL_PATCH     0x36310000u

#define W9_SHIFTED_CULL_ADDR    0x8001A6C0u
#define W9_SHIFTED_CULL_WORD    0x2AC10000u
#define W9_SHIFTED_CULL_PATCH   0x00000825u

static uint32_t s_clip_base;
static uint32_t s_clip_applied;
static uint32_t s_tile_count_a;
static uint32_t s_tile_count_b;
static uint32_t s_grid_start_a;
static uint32_t s_grid_start_b;

static uint32_t w9_tile_cols(int32_t margin)
{
    if (margin <= 0)
        return 1;

    return 1u + 2u * (uint32_t)((margin + 31) / 32);
}

static uint32_t w9_grid_margin(int32_t margin)
{
    if (margin <= 0)
        return 32;

    return 32u * (uint32_t)((margin + 62) / 32);
}

static void w9_patch_clip(int32_t margin)
{
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

static void w9_patch_imm16(uint32_t addr, uint32_t vanilla,
                           uint32_t *applied, uint32_t imm)
{
    uint32_t word = psx_mod_read_word(addr);

    if (word != vanilla && word != *applied)
        return;

    uint32_t want = (vanilla & 0xFFFF0000u) | (imm & 0xFFFFu);

    if (word != want)
        psx_mod_write_code_word(addr, want);

    *applied = want;
}

static void w9_patch_inner_cull(int32_t margin)
{
    uint32_t word = psx_mod_read_word(W9_INNER_CULL_ADDR);

    if (word != W9_INNER_CULL_WORD && word != W9_INNER_CULL_PATCH)
        return;

    uint32_t want = margin > 0
        ? W9_INNER_CULL_PATCH
        : W9_INNER_CULL_WORD;

    if (word != want)
        psx_mod_write_code_word(W9_INNER_CULL_ADDR, want);
}

static void w9_patch_shifted_cull(int32_t margin)
{
    uint32_t word = psx_mod_read_word(W9_SHIFTED_CULL_ADDR);

    if (word != W9_SHIFTED_CULL_WORD && word != W9_SHIFTED_CULL_PATCH)
        return;

    uint32_t want = margin > 0
        ? W9_SHIFTED_CULL_PATCH
        : W9_SHIFTED_CULL_WORD;

    if (word != want)
        psx_mod_write_code_word(W9_SHIFTED_CULL_ADDR, want);
}

static void wild9_widescreen_vblank(void)
{
    if (!psx_mod_game_started())
        return;

    int32_t margin = psx_mod_widescreen_x_margin();

    w9_patch_clip(margin);

    uint32_t tile_imm = margin > 0 ? w9_tile_cols(margin) : 1u;
    w9_patch_imm16(W9_TILE_COUNT_A_ADDR, W9_TILE_COUNT_A_WORD,
                   &s_tile_count_a, tile_imm);
    w9_patch_imm16(W9_TILE_COUNT_B_ADDR, W9_TILE_COUNT_B_WORD,
                   &s_tile_count_b, tile_imm);

    uint32_t grid_imm = margin > 0
        ? (uint32_t)(-(int32_t)w9_grid_margin(margin)) & 0xFFFFu
        : 0xFFE0u;

    w9_patch_imm16(W9_GRID_START_A_ADDR, W9_GRID_START_A_WORD,
                   &s_grid_start_a, grid_imm);
    w9_patch_imm16(W9_GRID_START_B_ADDR, W9_GRID_START_B_WORD,
                   &s_grid_start_b, grid_imm);

    w9_patch_inner_cull(margin);
    w9_patch_shifted_cull(margin);
}

static void wild9_widescreen_activate(void)
{
    psx_mod_set_fixed_display_aspect(16, 9);
}

PSX_MOD_CONSTRUCTOR(wild9_register_widescreen_plugin)
{
    (void)psx_mod_register_activation_plugin(
        "wild9.widescreen", wild9_widescreen_activate);
    (void)psx_mod_register_vblank_plugin(
        "wild9.widescreen", wild9_widescreen_vblank);
}
