#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Every actor keeps its hit points as a signed halfword at node+0x1C2; Wex's
 * 999 is the same field. Damage goes through 0x8002A0A8, which adds a
 * negative delta and flags the actor dead once the value reaches zero.
 *
 * Outside Wex's own code an actor gets its hit points in two places, so
 * scaling those covers every later hit without touching any damage source:
 *  - spawn: 0x80016BDC copies the class descriptor into node+0x190, and
 *    descriptor+0x32 lands on node+0x1C2;
 *  - script opcode 0x24, which sets node+0x1C2 from a 16-bit immediate.
 *
 * Boss bars (opcode 0xB7) show hp * 999 / max, where max is the unscaled
 * descriptor+0x32 the opcode stored in 0x800777D4, so max scales too.
 *
 * Resource 2 holds Wex and resource 37 the pickups and kill drops; both keep
 * their stock values.
 */

#define PKG_ENEMY_HEALTH "wild9.gameplay.enemy-health"
#define FEATURE_ENEMY_HEALTH "enemy_health"
#define PLUGIN_ENEMY_HEALTH "wild9.enemyhealth"

#define W9_DESC_INIT_FUNC 0x80016BDCu
#define W9_DESC_INIT_RETURN 0x80028DDCu /* after jal at 0x80028DD4 */
#define W9_OP_SET_HP_FUNC 0x80034400u
#define W9_OP_BOSS_BAR_FUNC 0x8003BA74u

#define W9_SCRIPT_PC 0x8007782Cu
#define W9_BOSS_BAR_MAX 0x800777D4u
#define W9_PLAYER_NODE 0x800777E0u

#define W9_SCRIPT_OBJECT 0x44u
#define W9_OBJECT_NODE 0x10u
#define W9_NODE_RESOURCE 0x0Au
#define W9_NODE_PROPS 0x190u
#define W9_NODE_HP 0x1C2u

#define W9_RES_PLAYER 2u
#define W9_RES_PICKUPS 37u

#define W9_BOSS_BAR_SETUP 0u

#define W9_PERCENT_MIN 50
#define W9_PERCENT_STOCK 100
#define W9_PERCENT_MAX 500
#define W9_PERCENT_STEP 10

static int s_percent = W9_PERCENT_STOCK;

static int w9_percent_option(void) {
  char text[16] = "";
  const char *p = text;
  int value = 0;

  if (!psx_mod_option_value(PKG_ENEMY_HEALTH, FEATURE_ENEMY_HEALTH,
                            "health_percent", text, sizeof(text)))
    return W9_PERCENT_STOCK;
  if (*p < '0' || *p > '9')
    return W9_PERCENT_STOCK;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10 + (*p - '0');
    if (value > W9_PERCENT_MAX)
      return W9_PERCENT_STOCK;
  }
  if (*p != '\0' || value < W9_PERCENT_MIN || value % W9_PERCENT_STEP != 0)
    return W9_PERCENT_STOCK;
  return value;
}

static int w9_ptr_ok(uint32_t address) {
  address &= 0x1FFFFFFFu;
  return address >= 0x1000u && address <= 0x1FFFFCu && (address & 3u) == 0u;
}

/* Rounds to nearest. A living actor never drops to 0, which would spawn it
 * dead, and the result stays within the signed halfword. */
static int32_t w9_scale(int32_t value) {
  int32_t scaled;

  if (value <= 0)
    return value;
  scaled = (value * s_percent + W9_PERCENT_STOCK / 2) / W9_PERCENT_STOCK;
  if (scaled < 1)
    return 1;
  if (scaled > 0x7FFF)
    return 0x7FFF;
  return scaled;
}

static int w9_node_scaled(uint32_t node) {
  uint16_t resource;

  if (!w9_ptr_ok(node))
    return 0;
  if ((node & 0x1FFFFFFFu) == (psx_mod_read_word(W9_PLAYER_NODE) & 0x1FFFFFFFu))
    return 0;
  resource = psx_mod_read_half(node + W9_NODE_RESOURCE);
  return resource != W9_RES_PLAYER && resource != W9_RES_PICKUPS;
}

static void w9_scale_node_hp(uint32_t node) {
  int32_t hp;

  if (!w9_node_scaled(node))
    return;
  hp = (int16_t)psx_mod_read_half(node + W9_NODE_HP);
  psx_mod_write_half(node + W9_NODE_HP, (uint16_t)w9_scale(hp));
}

static uint32_t w9_script_node(uint32_t context) {
  uint32_t object;

  if (!w9_ptr_ok(context))
    return 0;
  object = psx_mod_read_word(context + W9_SCRIPT_OBJECT);
  if (!w9_ptr_ok(object))
    return 0;
  return psx_mod_read_word(object + W9_OBJECT_NODE);
}

static int w9_desc_init(struct CPUState *cpu) {
  uint32_t props;

  if (s_percent == W9_PERCENT_STOCK)
    return 0;
  if ((cpu->gpr[31] & 0x1FFFFFFFu) != (W9_DESC_INIT_RETURN & 0x1FFFFFFFu)) {
    psx_mod_counter_add("wild9.enemyhealth.bad_ra", 1u);
    return 0;
  }
  props = cpu->gpr[4];
  func_override_call_original(cpu);
  w9_scale_node_hp(props - W9_NODE_PROPS);
  return 1;
}

static int w9_op_set_hp(struct CPUState *cpu) {
  uint32_t context;

  if (s_percent == W9_PERCENT_STOCK)
    return 0;
  context = cpu->gpr[4];
  func_override_call_original(cpu);
  w9_scale_node_hp(w9_script_node(context));
  return 1;
}

static int w9_op_boss_bar(struct CPUState *cpu) {
  uint32_t context;
  uint8_t sub;

  if (s_percent == W9_PERCENT_STOCK)
    return 0;
  context = cpu->gpr[4];
  sub = psx_mod_read_byte(psx_mod_read_word(W9_SCRIPT_PC));
  func_override_call_original(cpu);
  if (sub == W9_BOSS_BAR_SETUP && w9_node_scaled(w9_script_node(context)))
    psx_mod_write_word(
        W9_BOSS_BAR_MAX,
        (uint32_t)w9_scale((int32_t)psx_mod_read_word(W9_BOSS_BAR_MAX)));
  return 1;
}

static void w9_enemyhealth_activate(void) { s_percent = w9_percent_option(); }

static void w9_enemyhealth_session_reset(void) { s_percent = W9_PERCENT_STOCK; }

PSX_MOD_CONSTRUCTOR(wild9_register_enemyhealth_plugin) {
  /* addiu sp,sp,-0x18; sw ra,0x10(sp) */
  static const uint32_t desc_init_entry[2] = {0x27BDFFE8u, 0xAFBF0010u};
  /* lw v0,0x3C8(gp); nop */
  static const uint32_t op_set_hp_entry[2] = {0x8F8203C8u, 0x00000000u};
  /* addiu sp,sp,-0x20; lw v1,0x3C8(gp) */
  static const uint32_t op_boss_bar_entry[2] = {0x27BDFFE0u, 0x8F8303C8u};

  (void)psx_mod_register_activation_plugin(PLUGIN_ENEMY_HEALTH,
                                           w9_enemyhealth_activate);
  (void)psx_mod_register_session_reset_plugin(PLUGIN_ENEMY_HEALTH,
                                              w9_enemyhealth_session_reset);
  (void)psx_mod_register_function_override(PLUGIN_ENEMY_HEALTH ":spawn",
                                           W9_DESC_INIT_FUNC, w9_desc_init,
                                           desc_init_entry, 2, FO_CREDIT_SELF);
  (void)psx_mod_register_function_override(PLUGIN_ENEMY_HEALTH ":set_hp",
                                           W9_OP_SET_HP_FUNC, w9_op_set_hp,
                                           op_set_hp_entry, 2, FO_CREDIT_SELF);
  (void)psx_mod_register_function_override(
      PLUGIN_ENEMY_HEALTH ":boss_bar", W9_OP_BOSS_BAR_FUNC, w9_op_boss_bar,
      op_boss_bar_entry, 2, FO_CREDIT_SELF);
}
