#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * In analog mode (ID 0x73) the original pad reader (0x8001DF7C) masks the
 * button word with 0x0FFF, dropping the D-pad, and takes directions only from
 * the left stick. Digital mode keeps the D-pad bits and derives -0x80/+0x80
 * stick values from them (0x8001DED4).
 *
 * Both modes hand the button word to 0x8001DCE8, so this pre-hook puts the
 * D-pad back there. The D-pad replaces the stick per axis instead of adding to
 * it, because Left and Right held together freeze the actor scripts.
 */

#define FEATURE_ID "wild9.analog_dpad"

#define W9_PAD_STORE_FUNC 0x8001DCE8u
#define W9_PAD_STORE_ENTRY_WORD 0x27BDFFE0u /* addiu sp,sp,-0x20 */

#define W9_PAD_STATE_BASE 0x8007C788u
#define W9_PAD_STATE_STRIDE 0x10u
#define W9_PAD_STATE_LX 0x0Cu
#define W9_PAD_STATE_LY 0x0Eu

#define W9_PAD_BUFFER_BASE 0x8007C230u /* raw BIOS pad buffer */
#define W9_PAD_BUFFER_STRIDE 0x24u
#define W9_PAD_BUFFER_ID 0x01u
#define W9_PAD_BUFFER_BUTTONS 0x02u
#define W9_PAD_ID_ANALOG 0x73u

#define W9_PLAYERS 2u

/* Active high, as the game's button word after its byte swap. */
#define PAD_UP 0x1000u
#define PAD_RIGHT 0x2000u
#define PAD_DOWN 0x4000u
#define PAD_LEFT 0x8000u
#define PAD_DPAD (PAD_UP | PAD_RIGHT | PAD_DOWN | PAD_LEFT)

#define STICK_NEGATIVE 0xFF80u
#define STICK_POSITIVE 0x0080u

/* When exactly one D-pad direction of the axis is held, it replaces that axis
 * in buttons and the digital-mode stick value is returned. Otherwise returns 0
 * and leaves the axis to the stick. */
static uint32_t dpad_axis(uint32_t dpad, uint32_t negative, uint32_t positive,
                          uint32_t *buttons) {
  uint32_t axis = negative | positive;
  uint32_t held = dpad & axis;
  if (held != negative && held != positive)
    return 0u;
  *buttons = (*buttons & ~axis) | held;
  return held == negative ? STICK_NEGATIVE : STICK_POSITIVE;
}

static int analog_dpad_prehook(struct CPUState *cpu) {
  uint32_t state = cpu->gpr[4];
  uint32_t offset = state - W9_PAD_STATE_BASE;
  uint32_t player, buffer, raw, dpad, buttons, lx, ly;

  if (offset % W9_PAD_STATE_STRIDE != 0u ||
      offset / W9_PAD_STATE_STRIDE >= W9_PLAYERS)
    return 0;
  player = offset / W9_PAD_STATE_STRIDE;
  buffer = W9_PAD_BUFFER_BASE + player * W9_PAD_BUFFER_STRIDE;
  if (psx_mod_read_byte(buffer + W9_PAD_BUFFER_ID) != W9_PAD_ID_ANALOG)
    return 0;

  raw = psx_mod_read_half(buffer + W9_PAD_BUFFER_BUTTONS);
  dpad = ~(((raw & 0xFFu) << 8) | (raw >> 8)) & PAD_DPAD;
  if (dpad == 0u)
    return 0;

  buttons = cpu->gpr[5] & 0xFFFFu;
  lx = dpad_axis(dpad, PAD_LEFT, PAD_RIGHT, &buttons);
  ly = dpad_axis(dpad, PAD_UP, PAD_DOWN, &buttons);
  if (lx != 0u)
    psx_mod_write_half(state + W9_PAD_STATE_LX, (uint16_t)lx);
  if (ly != 0u)
    psx_mod_write_half(state + W9_PAD_STATE_LY, (uint16_t)ly);
  cpu->gpr[5] = buttons;
  return 0;
}

PSX_MOD_CONSTRUCTOR(wild9_register_analog_dpad_plugin) {
  static const uint32_t pad_store_entry[1] = {W9_PAD_STORE_ENTRY_WORD};

  /* The hook only rewrites the call's arguments and always declines, so the
   * original body runs and charges its own cycles. */
  (void)psx_mod_register_function_override(FEATURE_ID, W9_PAD_STORE_FUNC,
                                           analog_dpad_prehook, pad_store_entry,
                                           1, 0);
}
