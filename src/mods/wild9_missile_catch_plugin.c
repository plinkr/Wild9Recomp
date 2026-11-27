#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * Lets the Rig grab enemy missiles and throw them back. Every missile, thrown
 * head and triple missile the grunts fire is class 10 (GRUNT_MISSILE); only
 * its script changes from level to level.
 *
 * The Rig grabber (class 27, list 7) is never tested against list 8, the enemy
 * projectiles, so the beam passes through them even though
 * rig_beam_on_hit_grab would accept them. After collision_pass this plugin
 * runs that missing test. A grabbed missile moves to list 7, so its attack
 * boxes hit enemies and never Wex. Its script stops running, because the
 * flight code would overwrite the speeds the carry spring sets. Once
 * released, the missile homes on the enemy that fired it, or on the nearest
 * one ahead, using the missile's own aiming native.
 *
 * Whatever a caught missile hits sees class 19 (Wex's rocket) while its hit
 * handler runs, since many scripts react only to that class. Enemies that are
 * not bosses also lose the missile's attack value in hit points.
 *
 * Per-missile state lives in guest RAM so rollback and savestates carry it:
 * registers 8 to 13 of the missile's second script thread, which no longer
 * runs.
 */

#define PKG_MISSILE_CATCH "wild9.gameplay.missile-catch"
#define FEATURE_MISSILE_CATCH "missile_catch"
#define PLUGIN_MISSILE_CATCH "wild9.missile_catch"

#define W9_COLLISION_PASS 0x800291ECu
#define W9_COLLISION_TEST_PAIR 0x80029BC0u
#define W9_FIRE_EVENT 0x80029EF4u
#define W9_RUN_SCRIPT_THREADS 0x8004B070u
#define W9_RIG_RELEASE_HELD 0x8002C65Cu
#define W9_NATIVE_AIM 0x8002D360u /* script native 17 */

#define W9_ENTITY_LISTS 0x80081218u
#define W9_ENTITY_LIST_TAILS 0x80081278u
#define W9_RIG_HELD_NODE 0x80077864u
#define W9_SIN_TABLE 0x8006FE08u /* s16 sin, s16 cos per 1/4096 turn */

#define NODE_PREV 0x00u
#define NODE_NEXT 0x04u
#define NODE_ENTITY 0x10u
#define NODE_POS 0x2Cu /* s32 x, y, z in 20.12 */

#define ENT_SERIAL 0x000u
#define ENT_FLAGS 0x004u
#define ENT_LIST 0x008u
#define ENT_CLASS 0x00Au
#define ENT_MAIN_THREAD 0x0F8u
#define ENT_PROPS 0x190u
#define ENT_SPEED 0x198u
#define ENT_HP 0x1C2u
#define ENT_ATTACK 0x1C4u
#define ENT_HEIGHT 0x1C6u
#define ENT_IGNORE_LISTS 0x1CEu
#define ENT_VEL_X 0x1DCu
#define ENT_VEL_Y 0x1FCu
#define ENT_VEL_Z 0x21Cu
#define ENT_PATH_ANGLE 0x300u
#define ENT_TARGET 0x3B4u
#define ENT_TARGET_SERIAL 0x3BCu
#define ENT_PARENT 0x3D0u
#define ENT_HELD 0x3D4u
#define ENT_CONTACT 0x3D8u

/* Each velocity channel is the current speed, the speed the physics step
 * eases towards, and the cap. */
#define VEL_TARGET 0x4u
#define VEL_MAX 0x8u

#define THREAD_OBJECT 0x44u

/* Registers 8 to 13 of the thread at +0x94. */
#define ST_TAG 0x0BCu
#define ST_TIMER 0x0C0u
#define ST_TARGET 0x0C4u
#define ST_TARGET_SERIAL 0x0C8u
#define ST_VICTIM 0x0CCu
#define ST_HEADING 0x0D0u /* s16 x, s16 z world direction, 4096 = 1 */

#define TAG_HELD 0x4D430001u
#define TAG_THROWN 0x4D430002u

#define FLAG_DEAD 0x0001u
#define FLAG_REMOVE 0x0010u
#define FLAG_INACTIVE 0x0200u

#define PROP_COLLIDABLE 0x00000040u
#define PROP_ON_PATH 0x00000080u
#define PROP_INVULNERABLE 0x00002000u

#define LIST_ENEMY 2u
#define LIST_SPAWNER 3u
#define LIST_GRAPPLE 6u
#define LIST_WEX_SHOT 7u
#define LIST_ENEMY_SHOT 8u
#define ENTITY_LIST_COUNT 15u

#define CLASS_WEX 2
#define CLASS_GRUNT_MISSILE 10
#define CLASS_WEAPON1 19
#define CLASS_GRABBER 27

#define BOX_ATTACK 1u
#define BOX_ZONE 2u
#define BOX_HURT 3u

#define MAX_LIST_NODES 256
#define MAX_PICKED 128

/* The missiles' own speed, +0x198 >> 12, in every level. */
#define W9_MISSILE_SPEED 250
#define W9_SPEED_PERCENT_DEFAULT 80
#define W9_SPEED_PERCENT_MIN 40
#define W9_SPEED_PERCENT_MAX 200
#define W9_SPEED_CAP_FLOOR 400
/* 3 s of logic frames. */
#define W9_FLIGHT_FRAMES 90
/* Twice the vanilla beam reach. */
#define W9_SEEK_RANGE 12000
#define W9_MIN_THROW_SPEED (2 << 12)

static const int16_t k_boss_classes[] = {50, 180, 191, 212, 216, 228, 234};

static int s_speed = W9_MISSILE_SPEED * W9_SPEED_PERCENT_DEFAULT / 100;
static int s_homing = 1;

static int ptr_ok(uint32_t address) {
  address &= 0x1FFFFFFFu;
  return address >= 0x1000u && address <= 0x1FFFFCu && (address & 3u) == 0u;
}

static uint32_t rd32(uint32_t address) { return psx_mod_read_word(address); }
static int32_t rds32(uint32_t address) {
  return (int32_t)psx_mod_read_word(address);
}
static int16_t rds16(uint32_t address) {
  return (int16_t)psx_mod_read_half(address);
}
static void wr32(uint32_t address, uint32_t value) {
  psx_mod_write_word(address, value);
}

static uint32_t node_entity(uint32_t node) {
  uint32_t ent;
  if (!ptr_ok(node))
    return 0;
  ent = rd32(node + NODE_ENTITY);
  return ptr_ok(ent) ? ent : 0;
}

static uint32_t list_head(uint32_t list) {
  return rd32(W9_ENTITY_LISTS + 4u * list);
}

static int is_caught(uint32_t ent) {
  uint32_t tag;
  if (!ptr_ok(ent) || rds16(ent + ENT_CLASS) != CLASS_GRUNT_MISSILE ||
      psx_mod_read_byte(ent + ENT_LIST) != LIST_WEX_SHOT)
    return 0;
  tag = rd32(ent + ST_TAG);
  return tag == TAG_HELD || tag == TAG_THROWN;
}

static int is_boss(int16_t cls) {
  for (size_t i = 0; i < sizeof(k_boss_classes) / sizeof(k_boss_classes[0]);
       ++i)
    if (k_boss_classes[i] == cls)
      return 1;
  return 0;
}

static int option_text(const char *id, char *text, uint32_t size) {
  return psx_mod_option_value(PKG_MISSILE_CATCH, FEATURE_MISSILE_CATCH, id,
                              text, size);
}

static int speed_percent_option(void) {
  char text[16] = "";
  const char *p = text;
  int value = 0;
  if (!option_text("speed_percent", text, sizeof(text)))
    return W9_SPEED_PERCENT_DEFAULT;
  if (*p < '0' || *p > '9')
    return W9_SPEED_PERCENT_DEFAULT;
  for (; *p >= '0' && *p <= '9'; ++p) {
    value = value * 10 + (*p - '0');
    if (value > W9_SPEED_PERCENT_MAX)
      return W9_SPEED_PERCENT_DEFAULT;
  }
  if (*p != '\0' || value < W9_SPEED_PERCENT_MIN)
    return W9_SPEED_PERCENT_DEFAULT;
  return value;
}

static int homing_option(void) {
  char text[16] = "";
  if (!option_text("homing", text, sizeof(text)))
    return 1;
  return strcmp(text, "false") != 0;
}

static uint32_t isqrt64(uint64_t value) {
  uint64_t bit = (uint64_t)1 << 62, root = 0;
  while (bit > value)
    bit >>= 2;
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)root;
}

/* Same steps as entity_list_unlink (0x80029178) and entity_list_append
 * (0x800290EC). */
static void list_unlink(uint32_t node, uint32_t list) {
  const uint32_t prev = rd32(node + NODE_PREV);
  const uint32_t next = rd32(node + NODE_NEXT);
  if (prev)
    wr32(prev + NODE_NEXT, next);
  if (next)
    wr32(next + NODE_PREV, prev);
  if (list_head(list) == node)
    wr32(W9_ENTITY_LISTS + 4u * list, next);
  if (rd32(W9_ENTITY_LIST_TAILS + 4u * list) == node)
    wr32(W9_ENTITY_LIST_TAILS + 4u * list, prev);
}

static void list_append(uint32_t node, uint32_t list) {
  const uint32_t tail = rd32(W9_ENTITY_LIST_TAILS + 4u * list);
  wr32(node + NODE_NEXT, 0u);
  wr32(node + NODE_PREV, tail);
  if (tail)
    wr32(tail + NODE_NEXT, node);
  wr32(W9_ENTITY_LIST_TAILS + 4u * list, node);
  if (!list_head(list))
    wr32(W9_ENTITY_LISTS + 4u * list, node);
}

static int collect(uint32_t list, uint32_t *out, int cap) {
  int count = 0;
  uint32_t node = list_head(list);
  for (int guard = 0; guard < MAX_LIST_NODES && ptr_ok(node) && count < cap;
       ++guard) {
    if (node_entity(node))
      out[count++] = node;
    node = rd32(node + NODE_NEXT);
  }
  return count;
}

static int in_list(uint32_t node, uint32_t list) {
  uint32_t cur = list_head(list);
  for (int guard = 0; guard < MAX_LIST_NODES && ptr_ok(cur); ++guard) {
    if (cur == node)
      return 1;
    cur = rd32(cur + NODE_NEXT);
  }
  return 0;
}

static int ignores_list(uint32_t ent, uint32_t list) {
  return (rds16(ent + ENT_IGNORE_LISTS) >> list) & 1;
}

static int is_grabber(uint32_t ent) {
  return ent && rds16(ent + ENT_CLASS) == CLASS_GRABBER;
}

static int is_free_missile(uint32_t ent) {
  return ent && rds16(ent + ENT_CLASS) == CLASS_GRUNT_MISSILE &&
         psx_mod_read_byte(ent + ENT_LIST) == LIST_ENEMY_SHOT;
}

/* Applies the filters collision_test_lists would use with the grabber as A
 * (list 7) and a missile as B (list 8), except the grabber's ignore mask:
 * the game never pairs those lists, so its bit 8 means nothing.
 *
 * The beam boxes are type 1. The missile model has only type 1 boxes in
 * levels 1, 2 and 7, and type 2 as well in levels 4, 8 and 14, so both types
 * are tested. Touching the missile's type 1 box also fires its event 1, the
 * explosion, which w9_fire_event drops. */
static void grab_tests(CPUState *cpu, uint32_t ra) {
  static const uint32_t k_missile_boxes[] = {BOX_ZONE, BOX_ATTACK};
  uint32_t grabbers[MAX_PICKED], missiles[MAX_PICKED];
  int n_grabbers = 0, n_missiles = 0;
  uint32_t nodes[MAX_PICKED];
  int n = collect(LIST_WEX_SHOT, nodes, MAX_PICKED);

  for (int i = 0; i < n; ++i) {
    const uint32_t ent = node_entity(nodes[i]);
    if (is_grabber(ent) && (rd32(ent + ENT_PROPS) & PROP_COLLIDABLE) &&
        !(psx_mod_read_half(ent + ENT_FLAGS) & FLAG_INACTIVE))
      grabbers[n_grabbers++] = nodes[i];
  }
  if (!n_grabbers)
    return;
  n = collect(LIST_ENEMY_SHOT, nodes, MAX_PICKED);
  for (int i = 0; i < n; ++i) {
    const uint32_t ent = node_entity(nodes[i]);
    if (rds16(ent + ENT_CLASS) == CLASS_GRUNT_MISSILE &&
        (rd32(ent + ENT_PROPS) & PROP_COLLIDABLE) &&
        !(psx_mod_read_half(ent + ENT_FLAGS) & (FLAG_DEAD | FLAG_REMOVE)) &&
        !ignores_list(ent, LIST_WEX_SHOT))
      missiles[n_missiles++] = nodes[i];
  }

  for (int g = 0; g < n_grabbers; ++g) {
    const uint32_t gent = node_entity(grabbers[g]);
    for (int m = 0; m < n_missiles && !rd32(gent + ENT_HELD); ++m) {
      for (size_t b = 0;
           b < sizeof(k_missile_boxes) / sizeof(k_missile_boxes[0]) &&
           !rd32(gent + ENT_HELD);
           ++b)
        (void)psx_mod_call_guest(cpu, W9_COLLISION_TEST_PAIR, ra, BOX_ATTACK,
                                 grabbers[g], k_missile_boxes[b], missiles[m]);
    }
  }
}

static void raise_cap(uint32_t ent, uint32_t channel, uint32_t cap) {
  if (rd32(ent + channel + VEL_MAX) < cap)
    wr32(ent + channel + VEL_MAX, cap);
}

static void capture_held(void) {
  const uint32_t node = rd32(W9_RIG_HELD_NODE);
  const uint32_t ent = node_entity(node);
  const int floor = s_speed > W9_SPEED_CAP_FLOOR ? s_speed : W9_SPEED_CAP_FLOOR;
  if (!ent || rds16(ent + ENT_CLASS) != CLASS_GRUNT_MISSILE ||
      psx_mod_read_byte(ent + ENT_LIST) != LIST_ENEMY_SHOT)
    return;
  list_unlink(node, LIST_ENEMY_SHOT);
  psx_mod_write_byte(ent + ENT_LIST, (uint8_t)LIST_WEX_SHOT);
  list_append(node, LIST_WEX_SHOT);
  wr32(ent + ST_TAG, TAG_HELD);
  wr32(ent + ST_TIMER, 0u);
  wr32(ent + ST_TARGET, 0u);
  wr32(ent + ST_TARGET_SERIAL, 0u);
  wr32(ent + ST_VICTIM, 0u);
  wr32(ent + ST_HEADING, 0u);
  /* Its flight caps would otherwise slow the swings of the carry spring. */
  raise_cap(ent, ENT_VEL_X, (uint32_t)floor << 12);
  raise_cap(ent, ENT_VEL_Y, (uint32_t)floor << 12);
  raise_cap(ent, ENT_VEL_Z, (uint32_t)floor << 12);
}

static uint32_t find_grabber(uint32_t held_node) {
  uint32_t nodes[MAX_PICKED];
  const int n = collect(LIST_WEX_SHOT, nodes, MAX_PICKED);
  for (int i = 0; i < n; ++i) {
    const uint32_t ent = node_entity(nodes[i]);
    if (rds16(ent + ENT_CLASS) == CLASS_GRABBER &&
        (!held_node || rd32(ent + ENT_HELD) == held_node))
      return nodes[i];
  }
  return 0;
}

/* rig_beam_release_held takes a script thread; the grabber's main thread
 * points back at its node. */
static void release_hold(CPUState *cpu, uint32_t ra, uint32_t node) {
  const uint32_t grabber = find_grabber(node);
  const uint32_t gent = node_entity(grabber);
  if (!gent || rd32(gent + ENT_MAIN_THREAD + THREAD_OBJECT) != grabber)
    return;
  (void)psx_mod_call_guest(cpu, W9_RIG_RELEASE_HELD, ra, gent + ENT_MAIN_THREAD,
                           0u, 0u, 0u);
}

/* Event 1 with no contact takes every missile script to its explosion. */
static void detonate(CPUState *cpu, uint32_t ra, uint32_t ent) {
  wr32(ent + ENT_CONTACT, 0u);
  (void)psx_mod_call_guest(cpu, W9_FIRE_EVENT, ra, ent, BOX_ATTACK, 0u, 0u);
  if (!(psx_mod_read_half(ent + ENT_FLAGS) & FLAG_REMOVE))
    psx_mod_write_half(
        ent + ENT_FLAGS,
        (uint16_t)(psx_mod_read_half(ent + ENT_FLAGS) | FLAG_REMOVE));
}

static int enemy_alive(uint32_t ent) {
  const int16_t cls = rds16(ent + ENT_CLASS);
  return !(psx_mod_read_half(ent + ENT_FLAGS) &
           (FLAG_DEAD | FLAG_REMOVE | FLAG_INACTIVE)) &&
         (rd32(ent + ENT_PROPS) & PROP_COLLIDABLE) &&
         cls != CLASS_GRUNT_MISSILE && cls != CLASS_WEX;
}

static int target_valid(uint32_t node, uint32_t serial) {
  const uint32_t ent = node_entity(node);
  if (!ent || rd32(ent + ENT_SERIAL) != serial || !enemy_alive(ent))
    return 0;
  return in_list(node, LIST_ENEMY) || in_list(node, LIST_SPAWNER);
}

static void unpack_heading(uint32_t packed, int32_t *hx, int32_t *hz) {
  *hx = (int16_t)(packed >> 16);
  *hz = (int16_t)(packed & 0xFFFFu);
}

static uint32_t pack_heading(int64_t x, int64_t z) {
  const uint32_t len = isqrt64((uint64_t)(x * x + z * z));
  int32_t hx, hz;
  if (!len)
    return 0;
  hx = (int32_t)(x * 4096 / len);
  hz = (int32_t)(z * 4096 / len);
  return (uint32_t)(uint16_t)hx << 16 | (uint16_t)hz;
}

/* A path node's X channel runs along the path; path_signed_distance counts
 * forward as -(sin, cos) of the path angle. */
static uint32_t velocity_heading(uint32_t ent) {
  const int32_t vx = rds32(ent + ENT_VEL_X);
  if (rd32(ent + ENT_PROPS) & PROP_ON_PATH) {
    const uint32_t angle = psx_mod_read_half(ent + ENT_PATH_ANGLE) & 0xFFFu;
    const uint32_t e = rd32(W9_SIN_TABLE + angle * 4u);
    const int64_t s = (int16_t)(e >> 16), c = (int16_t)(e & 0xFFFFu);
    if (vx > -W9_MIN_THROW_SPEED && vx < W9_MIN_THROW_SPEED)
      return 0;
    return pack_heading(-s * (vx >> 12), -c * (vx >> 12));
  } else {
    const int32_t vz = rds32(ent + ENT_VEL_Z);
    const int64_t x = vx >> 12, z = vz >> 12;
    if (x * x + z * z <
        (int64_t)(W9_MIN_THROW_SPEED >> 12) * (W9_MIN_THROW_SPEED >> 12))
      return 0;
    return pack_heading(x, z);
  }
}

/* Aims at the middle of the body, as native 17 does. */
static void aim_point(uint32_t node, int32_t out[3]) {
  const uint32_t ent = node_entity(node);
  out[0] = rds32(node + NODE_POS) >> 12;
  out[1] = (rds32(node + NODE_POS + 4u) >> 12) - rds16(ent + ENT_HEIGHT) / 2;
  out[2] = rds32(node + NODE_POS + 8u) >> 12;
}

/* The closest living enemy ahead within range; the one that fired the missile
 * counts as half as far. With no heading every direction is ahead. */
static uint32_t pick_target(uint32_t ent, uint32_t node, uint32_t heading) {
  const uint32_t sender = rd32(ent + ENT_PARENT);
  int32_t hx, hz;
  uint32_t best = 0, best_score = 0xFFFFFFFFu;
  const int32_t mx = rds32(node + NODE_POS) >> 12;
  const int32_t my = rds32(node + NODE_POS + 4u) >> 12;
  const int32_t mz = rds32(node + NODE_POS + 8u) >> 12;
  unpack_heading(heading, &hx, &hz);

  for (uint32_t list = LIST_ENEMY; list <= LIST_SPAWNER; ++list) {
    uint32_t cur = list_head(list);
    for (int guard = 0; guard < MAX_LIST_NODES && ptr_ok(cur); ++guard) {
      const uint32_t tent = node_entity(cur);
      if (tent && enemy_alive(tent)) {
        int32_t p[3];
        int64_t dx, dy, dz;
        uint32_t dist;
        aim_point(cur, p);
        dx = p[0] - mx;
        dy = p[1] - my;
        dz = p[2] - mz;
        dist = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
        if ((!heading || dx * hx + dz * hz > 0) && dist <= W9_SEEK_RANGE) {
          const uint32_t score = cur == sender ? dist / 2u : dist;
          if (score < best_score) {
            best_score = score;
            best = cur;
          }
        }
      }
      cur = rd32(cur + NODE_NEXT);
    }
  }
  return best;
}

static void set_channel(uint32_t ent, uint32_t channel, int32_t value) {
  wr32(ent + channel, (uint32_t)value);
  wr32(ent + channel + VEL_TARGET, (uint32_t)value);
}

static void fly_straight(uint32_t ent, uint32_t heading) {
  const int32_t speed = s_speed << 12;
  int32_t hx, hz;
  unpack_heading(heading, &hx, &hz);
  if (rd32(ent + ENT_PROPS) & PROP_ON_PATH) {
    const uint32_t angle = psx_mod_read_half(ent + ENT_PATH_ANGLE) & 0xFFFu;
    const uint32_t e = rd32(W9_SIN_TABLE + angle * 4u);
    const int64_t along = -(int64_t)(int16_t)(e >> 16) * hx -
                          (int64_t)(int16_t)(e & 0xFFFFu) * hz;
    set_channel(ent, ENT_VEL_X, along >= 0 ? speed : -speed);
  } else {
    set_channel(ent, ENT_VEL_X, (int32_t)((int64_t)speed * hx / 4096));
    set_channel(ent, ENT_VEL_Z, (int32_t)((int64_t)speed * hz / 4096));
  }
  set_channel(ent, ENT_VEL_Y, 0);
}

static void set_target(uint32_t ent, uint32_t target) {
  const uint32_t tent = node_entity(target);
  wr32(ent + ST_TARGET, target);
  wr32(ent + ST_TARGET_SERIAL, tent ? rd32(tent + ENT_SERIAL) : 0u);
}

/* A throw too weak to give a direction sends the missile away from Wex. */
static void begin_flight(uint32_t ent, uint32_t node) {
  uint32_t heading = velocity_heading(ent);
  if (!heading) {
    const uint32_t gent = node_entity(find_grabber(0));
    const uint32_t wex = gent ? rd32(gent + ENT_PARENT) : 0u;
    if (ptr_ok(wex))
      heading = pack_heading(
          (int64_t)(rds32(node + NODE_POS) - rds32(wex + NODE_POS)) >> 12,
          (int64_t)(rds32(node + NODE_POS + 8u) - rds32(wex + NODE_POS + 8u)) >>
              12);
  }
  wr32(ent + ST_HEADING, heading);
  wr32(ent + ST_TIMER, 0u);
  wr32(ent + ST_TAG, TAG_THROWN);
  wr32(ent + ENT_SPEED, (uint32_t)s_speed << 12);
  set_target(ent, s_homing ? pick_target(ent, node, heading) : 0u);
}

static void fly(CPUState *cpu, uint32_t ra, uint32_t ent, uint32_t node) {
  const uint16_t flags = psx_mod_read_half(ent + ENT_FLAGS);
  uint32_t target = rd32(ent + ST_TARGET);
  const uint32_t timer = rd32(ent + ST_TIMER) + 1u;

  if (flags & FLAG_REMOVE)
    return;
  if ((flags & FLAG_DEAD) || timer > W9_FLIGHT_FRAMES) {
    detonate(cpu, ra, ent);
    return;
  }
  wr32(ent + ST_TIMER, timer);

  /* Without a target it keeps looking, so an enemy that comes into range
   * still draws it. */
  if (s_homing &&
      (!target || !target_valid(target, rd32(ent + ST_TARGET_SERIAL)))) {
    target = pick_target(ent, node, rd32(ent + ST_HEADING));
    set_target(ent, target);
  }
  if (target && rd32(ent + ENT_MAIN_THREAD + THREAD_OBJECT) == node) {
    const uint32_t heading = velocity_heading(ent);
    uint32_t pos[3];
    wr32(ent + ENT_TARGET, target);
    wr32(ent + ENT_TARGET_SERIAL, rd32(ent + ST_TARGET_SERIAL));
    /* Off a path, native 17 zeroes the node translation and only the next
     * motion step restores it. Called after this frame's motion step, the
     * missile would be drawn at the world origin. */
    for (uint32_t k = 0; k < 3; ++k)
      pos[k] = rd32(node + NODE_POS + 4u * k);
    (void)psx_mod_call_guest(cpu, W9_NATIVE_AIM, ra, ent + ENT_MAIN_THREAD, 0u,
                             0u, 0u);
    for (uint32_t k = 0; k < 3; ++k)
      wr32(node + NODE_POS + 4u * k, pos[k]);
    if (heading)
      wr32(ent + ST_HEADING, heading);
  } else {
    fly_straight(ent, rd32(ent + ST_HEADING));
  }
}

static void update_caught(CPUState *cpu, uint32_t ra) {
  uint32_t nodes[MAX_PICKED];
  const int n = collect(LIST_WEX_SHOT, nodes, MAX_PICKED);
  const uint32_t held = rd32(W9_RIG_HELD_NODE);

  for (int i = 0; i < n; ++i) {
    const uint32_t node = nodes[i];
    const uint32_t ent = node_entity(node);
    if (!is_caught(ent))
      continue;
    if (node == held) {
      const uint16_t flags = psx_mod_read_half(ent + ENT_FLAGS);
      /* The reaper frees it later this frame; drop it first so the Rig is
       * not left holding a freed node. */
      if (flags & (FLAG_DEAD | FLAG_REMOVE)) {
        release_hold(cpu, ra, node);
        if (!(flags & FLAG_REMOVE))
          detonate(cpu, ra, ent);
      }
      continue;
    }
    if (rd32(ent + ST_TAG) == TAG_HELD)
      begin_flight(ent, node);
    fly(cpu, ra, ent, node);
  }
}

/* A carried missile ignores crates, platforms and the like so it does not go
 * off while Wex moves it around. */
static int contact_allowed(int held, uint32_t list) {
  if (list == LIST_GRAPPLE)
    return 0;
  return !held || list == LIST_ENEMY || list == LIST_SPAWNER;
}

static int is_held(uint32_t ent) {
  return node_entity(rd32(W9_RIG_HELD_NODE)) == ent;
}

/* Same arithmetic as entity_apply_damage (0x8002A0A8). */
static void damage_enemy(uint32_t victim, uint32_t missile) {
  const uint16_t flags = psx_mod_read_half(victim + ENT_FLAGS);
  const int32_t attack = rds16(missile + ENT_ATTACK);
  int32_t hp;
  if (is_boss(rds16(victim + ENT_CLASS)) ||
      (flags & (FLAG_DEAD | FLAG_REMOVE)) ||
      (rd32(victim + ENT_PROPS) & PROP_INVULNERABLE) || attack <= 0 ||
      rd32(missile + ST_VICTIM) == victim)
    return;
  wr32(missile + ST_VICTIM, victim);
  hp = rds16(victim + ENT_HP) - attack;
  if (hp <= 0) {
    hp = 0;
    psx_mod_write_half(victim + ENT_FLAGS, (uint16_t)(flags | FLAG_DEAD));
  }
  psx_mod_write_half(victim + ENT_HP, (uint16_t)hp);
}

static int w9_fire_event(CPUState *cpu) {
  const uint32_t ent = cpu->gpr[4];
  const uint32_t event = cpu->gpr[5];
  uint32_t other_ent, list;

  if (event < BOX_ATTACK || event > BOX_HURT || !ptr_ok(ent))
    return 0;
  other_ent = node_entity(rd32(ent + ENT_CONTACT));
  if (!other_ent)
    return 0;

  /* Only grab_tests pairs the beam with a free missile, and the missile must
   * be grabbed, not explode on the beam. */
  if (is_free_missile(ent) && is_grabber(other_ent))
    return 1;

  if (is_caught(other_ent)) {
    list = psx_mod_read_byte(ent + ENT_LIST);
    if (!contact_allowed(is_held(other_ent), list))
      return 1;
    psx_mod_write_half(other_ent + ENT_CLASS, (uint16_t)CLASS_WEAPON1);
    func_override_call_original(cpu);
    psx_mod_write_half(other_ent + ENT_CLASS, (uint16_t)CLASS_GRUNT_MISSILE);
    if (event != BOX_ATTACK && (list == LIST_ENEMY || list == LIST_SPAWNER))
      damage_enemy(ent, other_ent);
    return 1;
  }
  if (event == BOX_ATTACK && is_caught(ent) &&
      !contact_allowed(is_held(ent), psx_mod_read_byte(other_ent + ENT_LIST)))
    return 1;
  return 0;
}

static int w9_run_script_threads(CPUState *cpu) {
  return is_caught(cpu->gpr[4]);
}

static int w9_collision_pass(CPUState *cpu) {
  const uint32_t ra = cpu->gpr[31];
  func_override_call_original(cpu);
  if (!psx_mod_game_started())
    return 1;
  grab_tests(cpu, ra);
  capture_held();
  update_caught(cpu, ra);
  return 1;
}

static void w9_missile_catch_activate(void) {
  s_speed = W9_MISSILE_SPEED * speed_percent_option() / 100;
  s_homing = homing_option();
}

PSX_MOD_CONSTRUCTOR(wild9_register_missile_catch_plugin) {
  /* addiu sp,sp,-0x18; li a0,1 */
  static const uint32_t collision_pass_entry[2] = {0x27BDFFE8u, 0x24040001u};
  /* addiu sp,sp,-0x80; sw s0,0x78(sp) */
  static const uint32_t fire_event_entry[2] = {0x27BDFF80u, 0xAFB00078u};
  /* addiu sp,sp,-0x20; sw s2,0x18(sp) */
  static const uint32_t run_threads_entry[2] = {0x27BDFFE0u, 0xAFB20018u};

  (void)psx_mod_register_activation_plugin(PLUGIN_MISSILE_CATCH,
                                           w9_missile_catch_activate);
  (void)psx_mod_register_function_override(
      PLUGIN_MISSILE_CATCH ":collision_pass", W9_COLLISION_PASS,
      w9_collision_pass, collision_pass_entry, 2, FO_CREDIT_SELF);
  (void)psx_mod_register_function_override(PLUGIN_MISSILE_CATCH ":fire_event",
                                           W9_FIRE_EVENT, w9_fire_event,
                                           fire_event_entry, 2, FO_CREDIT_SELF);
  /* A skipped thread run has no hardware analog, so it charges no cycles. */
  (void)psx_mod_register_function_override(
      PLUGIN_MISSILE_CATCH ":script_threads", W9_RUN_SCRIPT_THREADS,
      w9_run_script_threads, run_threads_entry, 2, 0);
}
