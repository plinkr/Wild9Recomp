#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Contact damage in Wild 9 is the other actor's attack value: Wex's script
 * runs op 0x77 "77 00 02 00", which takes node+0x1C4 of the actor stored in
 * Wex+0x3D8 by the collision test. The small green enemies have 111.
 *
 * Their script for recovering after the Rig drops them sets the attack to 0
 * (op 0x4C, var 0x12), plays the wobble, and then means to restore it with
 * "4D 00 12 6F". Op 0x4D copies a thread register into the variable, not an
 * immediate, so 0x6F reads thread+0x1C4 in a 0x64-byte thread, i.e. whatever
 * sits in the scratchpad there. While the Rig is out that is usually a GTE
 * fixed-point value such as 0x1000, and one touch takes 4096 HP from Wex,
 * who has 999. Negative values heal him instead.
 *
 * The override runs that one instruction as the intended immediate 111 and
 * leaves every other op 0x4D to the original.
 */

#define PLUGIN_ID "wild9.grabbed_enemy_fix"

#define W9_OP4D_FUNC 0x80036FBCu /* op 0x4D: entity var := thread register */
#define W9_SCRIPT_PC 0x8007782Cu /* points past the opcode byte */

#define W9_THREAD_OBJECT 0x44u
#define W9_OBJECT_NODE 0x10u
#define W9_NODE_ATTACK 0x1C4u /* entity var 0x12, halfword */

#define W9_OP4D_LENGTH 3u
#define W9_INTENDED_ATTACK 0x6Fu

/* Operands of the faulty op and the op after it (op 0x50: clear bit 1 of
 * var 0x43), so only this script matches. */
static const uint8_t k_pattern[] = {0x00, 0x12, 0x6F, 0x50, 0x00, 0x01,
                                    0x43, 0xFD, 0xFF, 0xFF, 0xFF};

static int w9_ptr_ok(uint32_t address) {
  address &= 0x1FFFFFFFu;
  return address >= 0x1000u && address <= 0x1FFFFCu && (address & 3u) == 0u;
}

static int w9_pattern_at(uint32_t pc) {
  uint32_t i;
  if (!w9_ptr_ok(pc & ~3u))
    return 0;
  for (i = 0; i < sizeof(k_pattern); ++i)
    if (psx_mod_read_byte(pc + i) != k_pattern[i])
      return 0;
  return 1;
}

static int w9_op4d(struct CPUState *cpu) {
  uint32_t pc = psx_mod_read_word(W9_SCRIPT_PC);
  uint32_t object, node;

  if (!w9_pattern_at(pc))
    return 0;
  /* Selector 0 is the thread's own object. */
  object = psx_mod_read_word(cpu->gpr[4] + W9_THREAD_OBJECT);
  if (w9_ptr_ok(object)) {
    node = psx_mod_read_word(object + W9_OBJECT_NODE);
    if (w9_ptr_ok(node))
      psx_mod_write_half(node + W9_NODE_ATTACK, W9_INTENDED_ATTACK);
  }
  psx_mod_write_word(W9_SCRIPT_PC, pc + W9_OP4D_LENGTH);
  cpu->gpr[2] = 1u; /* keep running the thread */
  return 1;
}

PSX_MOD_CONSTRUCTOR(wild9_register_grabbed_enemy_fix_plugin) {
  /* addiu sp,sp,-0x20; lw v0,0x3C8(gp) */
  static const uint32_t op4d_entry[2] = {0x27BDFFE0u, 0x8F8203C8u};

  /* About the original's instruction count, lookup and var write included. */
  (void)psx_mod_register_function_override(PLUGIN_ID, W9_OP4D_FUNC, w9_op4d,
                                           op4d_entry, 2, 55);
}
