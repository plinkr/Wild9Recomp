#include "mod_plugins.h"

#include <stdint.h>

/*
 * Holding Left and Right together freezes the original game: the picture stops
 * while the music keeps playing (riding Henry or the Beast, Wex vs. Karn).
 *
 * In the actor scripts, the left walk state jumps to the right walk state while
 * Right is held, and the right walk state jumps back while Left is held. No
 * opcode between those jumps yields the frame, and the script interpreter
 * (0x8003BDAC) only returns when one does, so the frame never ends.
 *
 * On a DualShock the game derives directions from the stick with an if/else,
 * so only a digital pad can report both. The recomp folds the stick onto the
 * D-pad of a digital pad, which makes D-pad Left plus stick Right enough to
 * trigger the freeze. Up and Down get the same treatment as a precaution.
 */

#define FEATURE_ID "wild9.opposing_directions"

/* Active low in the pad word. */
#define PAD_UP 0x0010u
#define PAD_RIGHT 0x0020u
#define PAD_DOWN 0x0040u
#define PAD_LEFT 0x0080u
#define PAD_HORIZONTAL (PAD_LEFT | PAD_RIGHT)
#define PAD_VERTICAL (PAD_UP | PAD_DOWN)

#define W9_PLAYERS 2u

typedef struct {
  uint32_t previous;
  uint32_t horizontal;
  uint32_t vertical;
} AxisState;

static AxisState s_state[W9_PLAYERS];

/* The direction pressed most recently wins; if both start on the same sample,
 * neither does until one is released. The runtime may resample the pad more
 * than once per frame, so a repeated sample must give the same result. */
static uint32_t resolve_axis(uint32_t held, uint32_t previous, uint32_t axis,
                             uint32_t *winner) {
  uint32_t pressed = held & axis;
  if (pressed != axis) {
    *winner = pressed;
    return held;
  }
  {
    uint32_t newly = pressed & ~previous;
    if (newly == axis)
      *winner = 0u;
    else if (newly != 0u)
      *winner = newly;
  }
  return (held & ~axis) | *winner;
}

static int opposing_directions_transform(const PSXModPadFrame *frame,
                                         PSXModPadOutput *out) {
  AxisState *state;
  uint32_t held, resolved;
  if (frame->player >= W9_PLAYERS)
    return 0;
  state = &s_state[frame->player];
  held = ~out->buttons & 0xFFFFu;
  resolved =
      resolve_axis(held, state->previous, PAD_HORIZONTAL, &state->horizontal);
  resolved =
      resolve_axis(resolved, state->previous, PAD_VERTICAL, &state->vertical);
  state->previous = held;
  if (resolved == held)
    return 0;
  out->buttons = ~resolved & 0xFFFFu;
  return 1;
}

static void opposing_directions_activate(void) {
  static const PSXModPadTransform transform = {
      sizeof(PSXModPadTransform),
      PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_DIGITAL) |
          PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_DUALSHOCK) |
          PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_JOGCON) |
          PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_NEGCON),
      PSX_MOD_PAD_DIGITAL, opposing_directions_transform};
  uint32_t player;
  for (player = 0; player < W9_PLAYERS; ++player) {
    s_state[player].previous = 0u;
    s_state[player].horizontal = 0u;
    s_state[player].vertical = 0u;
    (void)psx_mod_set_pad_transform(player, &transform);
  }
}

PSX_MOD_CONSTRUCTOR(wild9_register_opposing_directions_plugin) {
  (void)psx_mod_register_activation_plugin(FEATURE_ID,
                                           opposing_directions_activate);
}
