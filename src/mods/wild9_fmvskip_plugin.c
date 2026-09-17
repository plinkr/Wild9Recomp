#include "cpu_state.h"
#include "mod_plugins.h"
#include "sio.h"
#include <string.h>

#define PKG_FMV_SKIP "wild9.enhancement.fmvskip"
#define FEATURE_FMV_SKIP "fmv_skip"
#define W9_FMV_PLAYER_FUNC 0x8005706Cu
#define W9_FMV_UPDATE_FUNC 0x80056E80u
#define W9_FMV_CLEANUP_FUNC 0x80057024u
#define W9_CTX_GP_OFFSET 0x63Cu
#define W9_CTX_DONE_OFFSET 0x48u
#define W9_CTX_ID_OFFSET 0x60u

static int s_enabled = 0;
static uint16_t s_button_mask = 0x0008u;
static uint16_t s_previous_buttons = 0xFFFFu;
static int s_skip_latch = 0;
static uint32_t s_intro_id = 0;

static uint32_t wild9_intro_id(uint32_t return_address) {
  switch (return_address) {
  case 0x80010408u:
    return 3u;
  case 0x80010410u:
    return 5u;
  case 0x80010418u:
    return 4u;
  default:
    return 0u;
  }
}

static void wild9_fmvskip_activate(void) {
  char text[16] = "";

  s_enabled = 1;
  s_skip_latch = 0;
  s_intro_id = 0;
  s_previous_buttons = sio_get_pad_buttons();
  s_button_mask = 0x0008u;
  if (psx_mod_option_value(PKG_FMV_SKIP, FEATURE_FMV_SKIP, "skip_button", text,
                           sizeof(text))) {
    if (strcmp(text, "cross") == 0)
      s_button_mask = 0x4000u;
    else if (strcmp(text, "square") == 0)
      s_button_mask = 0x8000u;
    else if (strcmp(text, "triangle") == 0)
      s_button_mask = 0x1000u;
    else if (strcmp(text, "select") == 0)
      s_button_mask = 0x0001u;
  }
}

static void wild9_fmvskip_entry(CPUState *cpu, uint32_t address) {
  if (!s_enabled || !cpu || !psx_mod_game_started())
    return;

  if (address == W9_FMV_PLAYER_FUNC) {
    s_intro_id = wild9_intro_id(cpu->gpr[31]);
    if (s_intro_id != cpu->gpr[4])
      s_intro_id = 0;
    if (s_intro_id == 3u || s_intro_id == 0u) {
      s_skip_latch = 0;
      s_previous_buttons = sio_get_pad_buttons();
    }
    return;
  }

  if (address == W9_FMV_CLEANUP_FUNC) {
    if (s_intro_id == 4u)
      s_skip_latch = 0;
    s_intro_id = 0;
    return;
  }

  if (address != W9_FMV_UPDATE_FUNC || s_intro_id == 0u)
    return;

  const uint32_t sp = cpu->gpr[29];
  const uint32_t stack_phys = sp & 0x1FFFFFFFu;
  const int ram_stack = stack_phys <= 0x007FFFDCu;
  const int scratch_stack =
      stack_phys >= 0x1F800000u && stack_phys <= 0x1F8003DCu;
  if ((sp & 3u) != 0u || (!ram_stack && !scratch_stack))
    return;
  if (wild9_intro_id(psx_mod_read_word(sp + 32u)) != s_intro_id)
    return;

  const uint32_t ctx = psx_mod_read_word(cpu->gpr[28] + W9_CTX_GP_OFFSET);
  if ((ctx & 3u) != 0u || ctx == 0u || (ctx & 0x1FFFFFFFu) > 0x007FFF9Cu)
    return;
  if (psx_mod_read_word(ctx + W9_CTX_ID_OFFSET) != s_intro_id)
    return;

  const uint16_t buttons = sio_get_pad_buttons();
  if ((s_previous_buttons & (uint16_t)~buttons & s_button_mask) != 0u)
    s_skip_latch = 1;
  s_previous_buttons = buttons;

  if (s_skip_latch)
    psx_mod_write_word(ctx + W9_CTX_DONE_OFFSET, 1u);
}

PSX_MOD_CONSTRUCTOR(wild9_register_fmvskip_plugin) {
  (void)psx_mod_register_activation_plugin("wild9.fmvskip",
                                           wild9_fmvskip_activate);
  (void)psx_mod_register_function_entry_plugin(
      "wild9.fmvskip", W9_FMV_PLAYER_FUNC, wild9_fmvskip_entry);
  (void)psx_mod_register_function_entry_plugin(
      "wild9.fmvskip", W9_FMV_UPDATE_FUNC, wild9_fmvskip_entry);
  (void)psx_mod_register_function_entry_plugin(
      "wild9.fmvskip", W9_FMV_CLEANUP_FUNC, wild9_fmvskip_entry);
}