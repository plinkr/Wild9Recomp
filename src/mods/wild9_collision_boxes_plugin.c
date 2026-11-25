#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * Draws the collision boxes the game tests each frame and the trigger boxes
 * of the hidden warps.
 *
 * Each logic frame collision_boxes_reset (0x80024CFC) empties every actor's
 * box chains, scripts (op 0x56) and animation frames
 * (collision_add_model_boxes, 0x80024FD0) refill them, and collision_pass
 * tests them. Nothing touches the chains until the next logic frame, so the
 * frame hook reads exactly what was tested. Box types:
 *   1  attack: hits types 2 and 3 of the other side, including the Rig grab;
 *   2  touch or hurt zone: hit by type 1 and touched by body boxes;
 *   3  hurtbox: hit by type 1 only; Wex has one per body part;
 *   body: the actor's width and height, tested against type 2.
 *
 * A warp is a pair of TRIG records of class 25 sharing the warp id at
 * record+4. Subs 0 to 2 are sources whose type-2 box sends Wex to the sub 3 or
 * 4 record with the same id; only Wreckage and Drench have them. Source boxes
 * are built from the records, so they show before the trigger actor spawns.
 */

#define PKG_COLLISION_BOXES "wild9.debug.collision-boxes"
#define FEATURE_COLLISION_BOXES "collision_boxes"
#define PLUGIN_COLLISION_BOXES "wild9.collision_boxes"

/* Screen flash pass of level_logic_frame: runs after collision_pass, while
 * the frame's camera and OT are current. */
#define W9_FRAME_HOOK_FUNC 0x80015DC8u

#define W9_OT 0x800779A4u
#define W9_FRAME_BUFFER_INDEX 0x80077560u
#define W9_CAMERA 0x80077BA4u /* MATRIX*: s16 m[3][3], s32 t[3] at +0x14 */
#define W9_SCREEN_W 0x80081258u
#define W9_SCREEN_H 0x8008125Au
#define W9_TRIG 0x8007768Cu /* u32 count, records* (0x30 bytes each) */
#define W9_ENTITY_LISTS 0x80081218u
#define W9_BOX_POOL 0x8007D818u

#define REC_SIZE 0x30u
#define REC_ID 0x04u
#define REC_ACTOR 0x08u
#define REC_CLASS 0x0Cu
#define REC_SUB 0x0Eu
#define REC_XYZ 0x20u

#define NODE_NEXT 0x04u
#define NODE_ENTITY 0x10u
#define NODE_POS 0x2Cu /* s32 x, y, z in 20.12 */

#define ENT_FLAGS 0x004u
#define ENT_CLASS 0x00Au
#define ENT_PROPS 0x190u
#define ENT_HEIGHT 0x1C6u
#define ENT_WIDTH 0x1D8u
#define ENT_BOX_COUNT 0x30Cu /* s8 per box type */
#define ENT_BOX_HEAD 0x314u  /* u32 per box type */

/* Pool entries hold s32 min[3] and max[3] in 20.12. */
#define BOX_SIZE 0x20u
#define BOX_POOL_COUNT 240u
#define BOX_NEXT 0x00u
#define BOX_MIN 0x08u
#define BOX_MAX 0x14u

#define FLAG_INACTIVE 0x200u
#define PROP_COLLIDABLE 0x40u

#define ENTITY_LIST_COUNT 15u
#define MAX_LIST_NODES 256
#define MAX_TYPE_BOXES 64

#define CLASS_WARP 25
#define CLASS_MIDBOSS 50
#define CLASS_FILBERT 180
#define CLASS_BEAST 191
#define CLASS_MB_ARM 212
#define CLASS_MB_HEAD 216
#define CLASS_KARN 228
#define CLASS_BIGEGM 234
#define WARP_DEST_SUB 3
#define MAX_RECORDS 2048u
#define DEST_HALF 250

/* OT slot 2. Slot 0 is drawn last, so the boxes land after the level, which
 * keeps them visible behind walls, and before the HUD text in slots 0-1. */
#define OT_BOX_SLOT_OFFSET 8u

#define NEAR_Z 64.0f
#define MAX_LINES 4096
#define MAX_TRIS 512
/* Tag plus 3 words per line, tag plus 4 per triangle, and the blend mode. */
#define ARENA_BYTES ((uint32_t)(MAX_LINES * 16 + MAX_TRIS * 20 + 64))

#define GP0_E1_ADDITIVE 0xE1000620u /* dither, draw to display, B+F */
#define GP0_LINE_F2 0x40000000u
#define GP0_POLY_F3_SEMI 0x22000000u

/* GP0 colours are 0xBBGGRR. */
#define COLOR_ATTACK 0x3030FFu
#define COLOR_ZONE 0x00E0FFu
#define COLOR_HURT 0x40FF40u
#define COLOR_BODY 0xFFFFFFu

enum {
  CAT_NONE,
  CAT_WEX,
  CAT_RIG,
  CAT_ENEMY,
  CAT_BOSS,
  CAT_WEX_SHOT,
  CAT_ENEMY_SHOT,
  CAT_HAZARD,
  CAT_OBJECT,
  CAT_TRIGGER,
  CAT_COUNT
};

typedef struct {
  int32_t m[9];
  int32_t t[3];
  float ofx, ofy, h;
  float xmin, xmax, ymin, ymax;
} View;

typedef struct {
  float x, y, z;
} Vec3;

typedef struct {
  float x, y;
} Vec2;

typedef struct {
  int16_t x0, y0, x1, y1;
  uint32_t color;
} Line;

typedef struct {
  int16_t x[3], y[3];
  uint32_t color;
} Tri;

static int s_enabled;
static int s_show_warps;
static int s_show_destinations;
static int s_show[CAT_COUNT];
static uint32_t s_arena[2];

static Line s_lines[MAX_LINES];
static int s_line_count;
static Tri s_tris[MAX_TRIS];
static int s_tri_count;

/* Indexed by warp id modulo 6. The ids on the disc, 123 to 125, get cyan,
 * magenta and orange so they do not clash with the box type colours. */
static const uint32_t k_warp_palette[] = {0xFF8040u, 0xC08080u, 0x80C0C0u,
                                          0xFFFF40u, 0xFF40FFu, 0x0080FFu};

/* The classes with a boss health bar (op 0xB7), the mid-boss parts, which
 * live in other lists, and the Beast, which has no bar but 999 HP. */
static const int16_t k_boss_classes[] = {
    CLASS_MIDBOSS, CLASS_MB_ARM, CLASS_MB_HEAD, CLASS_FILBERT,
    CLASS_BEAST,   CLASS_BIGEGM, CLASS_KARN};

static const uint32_t k_type_colors[4] = {0u, COLOR_ATTACK, COLOR_ZONE,
                                          COLOR_HURT};

static int option_flag(const char *option_id, int fallback) {
  char text[16] = "";
  if (!psx_mod_option_value(PKG_COLLISION_BOXES, FEATURE_COLLISION_BOXES,
                            option_id, text, sizeof(text)))
    return fallback;
  return strcmp(text, "true") == 0;
}

static int ptr_ok(uint32_t address) {
  address &= 0x1FFFFFFFu;
  return address >= 0x1000u && address <= 0x1FFFFCu && (address & 3u) == 0u;
}

static int box_ok(uint32_t address) {
  const uint32_t pool = W9_BOX_POOL & 0x1FFFFFFFu;
  address &= 0x1FFFFFFFu;
  return address >= pool && address < pool + BOX_POOL_COUNT * BOX_SIZE &&
         (address - pool) % BOX_SIZE == 0u;
}

static int32_t read_s32(uint32_t address) {
  return (int32_t)psx_mod_read_word(address);
}

static int16_t read_s16(uint32_t address) {
  return (int16_t)psx_mod_read_half(address);
}

static uint32_t warp_color(uint32_t id) {
  return k_warp_palette[id % (sizeof(k_warp_palette) /
                              sizeof(k_warp_palette[0]))];
}

static uint32_t dim(uint32_t color) { return (color >> 1) & 0x7F7F7Fu; }

static int read_view(CPUState *cpu, View *v) {
  const uint32_t cam = psx_mod_read_word(W9_CAMERA);
  int32_t w, h;
  if (!ptr_ok(cam))
    return 0;
  for (int i = 0; i < 9; ++i)
    v->m[i] = read_s16(cam + 2u * (uint32_t)i);
  for (int i = 0; i < 3; ++i)
    v->t[i] = read_s32(cam + 0x14u + 4u * (uint32_t)i);
  v->ofx = (float)(int32_t)gte_read_ctrl(cpu, 24) / 65536.0f;
  v->ofy = (float)(int32_t)gte_read_ctrl(cpu, 25) / 65536.0f;
  v->h = (float)(gte_read_ctrl(cpu, 26) & 0xFFFFu);
  w = read_s16(W9_SCREEN_W);
  h = read_s16(W9_SCREEN_H);
  if (v->h <= 0.0f || w <= 0 || w > 1024 || h <= 0 || h > 512)
    return 0;
  v->xmin = (float)-psx_mod_widescreen_view_x_margin();
  v->xmax = (float)(w - 1 + psx_mod_widescreen_view_x_margin());
  v->ymin = 0.0f;
  v->ymax = (float)(h - 1);
  return 1;
}

/* Same transform as trig_spawn_update: camera = R * world / 4096 + t. */
static Vec3 to_camera(const View *v, int32_t x, int32_t y, int32_t z) {
  Vec3 c;
  float out[3];
  for (int i = 0; i < 3; ++i) {
    const int64_t r = (int64_t)v->m[i * 3] * x + (int64_t)v->m[i * 3 + 1] * y +
                      (int64_t)v->m[i * 3 + 2] * z;
    out[i] = (float)r / 4096.0f + (float)v->t[i];
  }
  c.x = out[0];
  c.y = out[1];
  c.z = out[2];
  return c;
}

static Vec2 project(const View *v, Vec3 c) {
  Vec2 s;
  s.x = v->ofx + v->h * c.x / c.z;
  s.y = v->ofy + v->h * c.y / c.z;
  return s;
}

static int outcode(const View *v, Vec2 p) {
  int code = 0;
  if (p.x < v->xmin)
    code |= 1;
  else if (p.x > v->xmax)
    code |= 2;
  if (p.y < v->ymin)
    code |= 4;
  else if (p.y > v->ymax)
    code |= 8;
  return code;
}

static void add_line(const View *v, Vec2 a, Vec2 b, uint32_t color) {
  int ca = outcode(v, a), cb = outcode(v, b);
  for (int guard = 0; guard < 8; ++guard) {
    if (!(ca | cb)) {
      Line *l;
      if (s_line_count >= MAX_LINES)
        return;
      l = &s_lines[s_line_count++];
      l->x0 = (int16_t)a.x;
      l->y0 = (int16_t)a.y;
      l->x1 = (int16_t)b.x;
      l->y1 = (int16_t)b.y;
      l->color = color;
      return;
    }
    if (ca & cb)
      return;
    {
      const int out = ca ? ca : cb;
      Vec2 p;
      if (out & 8) {
        p.x = a.x + (b.x - a.x) * (v->ymax - a.y) / (b.y - a.y);
        p.y = v->ymax;
      } else if (out & 4) {
        p.x = a.x + (b.x - a.x) * (v->ymin - a.y) / (b.y - a.y);
        p.y = v->ymin;
      } else if (out & 2) {
        p.y = a.y + (b.y - a.y) * (v->xmax - a.x) / (b.x - a.x);
        p.x = v->xmax;
      } else {
        p.y = a.y + (b.y - a.y) * (v->xmin - a.x) / (b.x - a.x);
        p.x = v->xmin;
      }
      if (out == ca) {
        a = p;
        ca = outcode(v, a);
      } else {
        b = p;
        cb = outcode(v, b);
      }
    }
  }
}

/* Edges cross the camera plane when the box is beside or around the camera,
 * so they are cut at NEAR_Z before projecting. */
static void add_edge(const View *v, Vec3 a, Vec3 b, uint32_t color) {
  if (a.z < NEAR_Z && b.z < NEAR_Z)
    return;
  if (a.z < NEAR_Z || b.z < NEAR_Z) {
    const float t = (NEAR_Z - a.z) / (b.z - a.z);
    Vec3 p;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.z = NEAR_Z;
    if (a.z < NEAR_Z)
      a = p;
    else
      b = p;
  }
  add_line(v, project(v, a), project(v, b), color);
}

/* Sutherland-Hodgman against one screen edge. axis 0 = x, 1 = y. */
static int clip_poly(const Vec2 *in, int n, Vec2 *out, int axis, float limit,
                     int keep_less) {
  int count = 0;
  for (int i = 0; i < n; ++i) {
    const Vec2 a = in[i], b = in[(i + 1) % n];
    const float va = axis ? a.y : a.x, vb = axis ? b.y : b.x;
    const int ina = keep_less ? va <= limit : va >= limit;
    const int inb = keep_less ? vb <= limit : vb >= limit;
    if (ina)
      out[count++] = a;
    if (ina != inb) {
      const float t = (limit - va) / (vb - va);
      Vec2 p;
      p.x = a.x + (b.x - a.x) * t;
      p.y = a.y + (b.y - a.y) * t;
      out[count++] = p;
    }
  }
  return count;
}

static void add_face(const View *v, const Vec3 *cam, const int *idx,
                     uint32_t color) {
  Vec2 a[12], b[12];
  int n = 4;
  for (int i = 0; i < 4; ++i) {
    if (cam[idx[i]].z < NEAR_Z)
      return;
    a[i] = project(v, cam[idx[i]]);
  }
  n = clip_poly(a, n, b, 0, v->xmin, 0);
  n = clip_poly(b, n, a, 0, v->xmax, 1);
  n = clip_poly(a, n, b, 1, v->ymin, 0);
  n = clip_poly(b, n, a, 1, v->ymax, 1);
  for (int i = 1; i + 1 < n && s_tri_count < MAX_TRIS; ++i) {
    Tri *t = &s_tris[s_tri_count++];
    t->x[0] = (int16_t)a[0].x;
    t->y[0] = (int16_t)a[0].y;
    t->x[1] = (int16_t)a[i].x;
    t->y[1] = (int16_t)a[i].y;
    t->x[2] = (int16_t)a[i + 1].x;
    t->y[2] = (int16_t)a[i + 1].y;
    t->color = color;
  }
}

/* Corner i has x = max if bit 0, y = max if bit 1, z = max if bit 2. Only the
 * faces turned to the camera are filled, so the fill is the box silhouette
 * without overlapping layers. */
static void add_box(const View *v, const int32_t lo[3], const int32_t hi[3],
                    uint32_t color, int fill) {
  static const int faces[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                  {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
  Vec3 cam[8];
  for (int i = 0; i < 8; ++i)
    cam[i] = to_camera(v, (i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1],
                       (i & 4) ? hi[2] : lo[2]);
  for (int i = 0; i < 8; ++i)
    for (int bit = 1; bit < 8; bit <<= 1)
      if (!(i & bit))
        add_edge(v, cam[i], cam[i | bit], color);
  if (!fill)
    return;
  for (int f = 0; f < 6; ++f) {
    const int axis = f >> 1;
    const float sign = (f & 1) ? 1.0f : -1.0f;
    /* Outward normal in camera space: column `axis` of R. */
    const float nx = sign * (float)v->m[axis];
    const float ny = sign * (float)v->m[3 + axis];
    const float nz = sign * (float)v->m[6 + axis];
    const Vec3 p = cam[faces[f][0]];
    if (nx * p.x + ny * p.y + nz * p.z < 0.0f)
      add_face(v, cam, faces[f], dim(color));
  }
}

/* The op 0x56 box the class-25 script adds for each source sub. */
static int source_box(int16_t sub, int32_t *off_y, int32_t *half_xz,
                      int32_t *half_y) {
  switch (sub) {
  case 0:
    *off_y = -2000;
    *half_xz = 300;
    *half_y = 2000;
    return 1;
  case 1:
    *off_y = -200;
    *half_xz = 200;
    *half_y = 200;
    return 1;
  case 2:
    *off_y = -2000;
    *half_xz = 2000;
    *half_y = 2000;
    return 1;
  default:
    return 0;
  }
}

/* A spawned actor holds the live position; otherwise the record's own. */
static void record_position(uint32_t rec, int32_t pos[3]) {
  const uint32_t actor = psx_mod_read_word(rec + REC_ACTOR);
  if (ptr_ok(actor)) {
    for (int i = 0; i < 3; ++i)
      pos[i] = read_s32(actor + NODE_POS + 4u * (uint32_t)i) >> 12;
    return;
  }
  for (int i = 0; i < 3; ++i)
    pos[i] = read_s32(rec + REC_XYZ + 4u * (uint32_t)i);
}

static void add_warps(const View *v) {
  const uint32_t trig = psx_mod_read_word(W9_TRIG);
  uint32_t count, records;
  if (!ptr_ok(trig))
    return;
  count = psx_mod_read_word(trig);
  records = psx_mod_read_word(trig + 4u);
  if (count > MAX_RECORDS || !ptr_ok(records))
    return;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t rec = records + i * REC_SIZE;
    const int16_t sub = read_s16(rec + REC_SUB);
    const uint32_t color = warp_color(psx_mod_read_word(rec + REC_ID));
    int32_t pos[3], lo[3], hi[3], off_y, half_xz, half_y;
    if (read_s16(rec + REC_CLASS) != CLASS_WARP)
      continue;
    record_position(rec, pos);
    if (source_box(sub, &off_y, &half_xz, &half_y)) {
      lo[0] = pos[0] - half_xz;
      hi[0] = pos[0] + half_xz;
      lo[1] = pos[1] + off_y - half_y;
      hi[1] = pos[1] + off_y + half_y;
      lo[2] = pos[2] - half_xz;
      hi[2] = pos[2] + half_xz;
      add_box(v, lo, hi, color, 1);
    } else if (sub >= WARP_DEST_SUB && s_show_destinations) {
      lo[0] = pos[0] - DEST_HALF;
      hi[0] = pos[0] + DEST_HALF;
      lo[1] = pos[1] - 2 * DEST_HALF;
      hi[1] = pos[1];
      lo[2] = pos[2] - DEST_HALF;
      hi[2] = pos[2] + DEST_HALF;
      add_box(v, lo, hi, color, 0);
    }
  }
}

/* Lists 13 (water, fans and other area volumes) and 14 (never tested) are
 * left out. */
static int list_category(uint32_t list) {
  switch (list) {
  case 0:
    return CAT_WEX;
  case 1:
    return CAT_RIG;
  case 2:
  case 3:
    return CAT_ENEMY;
  case 4:
    return CAT_HAZARD;
  case 5:
  case 6:
    return CAT_OBJECT;
  case 7:
    return CAT_WEX_SHOT;
  case 8:
    return CAT_ENEMY_SHOT;
  case 9:
  case 10:
  case 11:
  case 12:
    return CAT_TRIGGER;
  default:
    return CAT_NONE;
  }
}

static int is_boss_class(int16_t cls) {
  for (size_t i = 0; i < sizeof(k_boss_classes) / sizeof(k_boss_classes[0]);
       ++i)
    if (k_boss_classes[i] == cls)
      return 1;
  return 0;
}

static void add_chain(const View *v, uint32_t ent, uint32_t type) {
  int count = (int8_t)psx_mod_read_byte(ent + ENT_BOX_COUNT + type);
  uint32_t box = psx_mod_read_word(ent + ENT_BOX_HEAD + 4u * type);
  if (count > MAX_TYPE_BOXES)
    count = MAX_TYPE_BOXES;
  for (int i = 0; i < count && box_ok(box); ++i) {
    int32_t lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
      lo[k] = read_s32(box + BOX_MIN + 4u * (uint32_t)k) >> 12;
      hi[k] = read_s32(box + BOX_MAX + 4u * (uint32_t)k) >> 12;
    }
    add_box(v, lo, hi, k_type_colors[type], 0);
    box = psx_mod_read_word(box + BOX_NEXT);
  }
}

/* Body box (type 6), as collision_test_entity_pair builds it. */
static void add_body(const View *v, uint32_t node, uint32_t ent) {
  const int32_t half_xz = read_s16(ent + ENT_WIDTH) / 2;
  const int32_t height = read_s16(ent + ENT_HEIGHT);
  int32_t pos[3], lo[3], hi[3];
  for (int i = 0; i < 3; ++i)
    pos[i] = read_s32(node + NODE_POS + 4u * (uint32_t)i) >> 12;
  lo[0] = pos[0] - half_xz;
  hi[0] = pos[0] + half_xz;
  lo[1] = pos[1] - height;
  hi[1] = pos[1];
  lo[2] = pos[2] - half_xz;
  hi[2] = pos[2] + half_xz;
  add_box(v, lo, hi, COLOR_BODY, 0);
}

/* Same filter collision_test_lists applies before testing an actor. */
static void add_actor(const View *v, uint32_t node, uint32_t ent, int cat) {
  if ((psx_mod_read_half(ent + ENT_FLAGS) & FLAG_INACTIVE) ||
      !(psx_mod_read_word(ent + ENT_PROPS) & PROP_COLLIDABLE))
    return;
  for (uint32_t type = 1; type <= 3; ++type)
    add_chain(v, ent, type);
  if (cat == CAT_WEX)
    add_body(v, node, ent);
}

static void add_actors(const View *v) {
  for (uint32_t list = 0; list < ENTITY_LIST_COUNT; ++list) {
    const int list_cat = list_category(list);
    uint32_t node = psx_mod_read_word(W9_ENTITY_LISTS + 4u * list);
    if (list_cat == CAT_NONE)
      continue;
    for (int guard = 0; guard < MAX_LIST_NODES && ptr_ok(node); ++guard) {
      const uint32_t ent = psx_mod_read_word(node + NODE_ENTITY);
      if (ptr_ok(ent)) {
        const int16_t cls = read_s16(ent + ENT_CLASS);
        const int cat = is_boss_class(cls) ? CAT_BOSS : list_cat;
        if (cls != CLASS_WARP && s_show[cat])
          add_actor(v, node, ent, cat);
      }
      node = psx_mod_read_word(node + NODE_NEXT);
    }
  }
}

static uint32_t xy(int16_t x, int16_t y) {
  return (uint32_t)(uint16_t)y << 16 | (uint16_t)x;
}

static int link_prim(uint32_t *p, uint32_t end, uint32_t slot,
                     const uint32_t *words, uint32_t n) {
  if (*p + 4u * (n + 1u) > end)
    return 0;
  for (uint32_t i = 0; i < n; ++i)
    psx_mod_write_word(*p + 4u + 4u * i, words[i]);
  psx_mod_write_word(*p, n << 24 | (psx_mod_read_word(slot) & 0xFFFFFFu));
  psx_mod_write_word(slot, *p & 0xFFFFFFu);
  *p += 4u * (n + 1u);
  return 1;
}

/* Linking prepends, so the slot draws in reverse order: the blend mode first,
 * then the fills, then the outlines on top. */
static void emit(uint32_t slot, uint32_t arena) {
  const uint32_t end = arena + ARENA_BYTES;
  uint32_t p = arena;
  for (int i = 0; i < s_line_count; ++i) {
    const Line *l = &s_lines[i];
    const uint32_t w[3] = {GP0_LINE_F2 | l->color, xy(l->x0, l->y0),
                           xy(l->x1, l->y1)};
    if (!link_prim(&p, end, slot, w, 3))
      return;
  }
  for (int i = 0; i < s_tri_count; ++i) {
    const Tri *t = &s_tris[i];
    const uint32_t w[4] = {GP0_POLY_F3_SEMI | t->color, xy(t->x[0], t->y[0]),
                           xy(t->x[1], t->y[1]), xy(t->x[2], t->y[2])};
    if (!link_prim(&p, end, slot, w, 4))
      return;
  }
  if (s_tri_count) {
    const uint32_t w[1] = {GP0_E1_ADDITIVE};
    (void)link_prim(&p, end, slot, w, 1);
  }
}

static int wild9_collision_boxes_frame(struct CPUState *cpu) {
  View v;
  uint32_t ot, arena;
  if (!s_enabled)
    return 0;
  ot = psx_mod_read_word(W9_OT);
  arena = s_arena[psx_mod_read_word(W9_FRAME_BUFFER_INDEX) & 1u];
  if (!ptr_ok(ot) || !arena || !read_view(cpu, &v))
    return 0;
  s_line_count = s_tri_count = 0;
  /* Warps first, so they keep their lines if the cap is reached. */
  if (s_show_warps)
    add_warps(&v);
  add_actors(&v);
  if (s_line_count >= MAX_LINES)
    psx_mod_counter_add("wild9.collision_boxes.line_cap", 1u);
  emit(ot + OT_BOX_SLOT_OFFSET, arena);
  return 0;
}

static void wild9_collision_boxes_activate(void) {
  s_enabled = 0;
  s_show_warps = option_flag("show_warps", 1);
  s_show_destinations = option_flag("show_destinations", 1);
  s_show[CAT_NONE] = 0;
  s_show[CAT_WEX] = option_flag("show_wex", 1);
  s_show[CAT_RIG] = option_flag("show_rig", 1);
  s_show[CAT_ENEMY] = option_flag("show_enemies", 1);
  s_show[CAT_BOSS] = option_flag("show_bosses", 1);
  s_show[CAT_WEX_SHOT] = option_flag("show_wex_projectiles", 1);
  s_show[CAT_ENEMY_SHOT] = option_flag("show_enemy_projectiles", 1);
  s_show[CAT_HAZARD] = option_flag("show_hazards", 0);
  s_show[CAT_OBJECT] = option_flag("show_objects", 0);
  s_show[CAT_TRIGGER] = option_flag("show_triggers", 0);
  if (!s_arena[0]) {
    s_arena[0] = psx_mod_alloc_gpu_dma_memory(ARENA_BYTES, 4u);
    s_arena[1] = psx_mod_alloc_gpu_dma_memory(ARENA_BYTES, 4u);
  }
  if (!s_arena[0] || !s_arena[1]) {
    psx_mod_counter_add("wild9.collision_boxes.no_arena", 1u);
    return;
  }
  s_enabled = 1;
}

static void wild9_collision_boxes_session_reset(void) { s_enabled = 0; }

PSX_MOD_CONSTRUCTOR(wild9_register_collision_boxes_plugin) {
  /* addiu sp,sp,-0x20; lui v0,0x8008 */
  static const uint32_t frame_hook_entry[2] = {0x27BDFFE0u, 0x3C028008u};

  (void)psx_mod_register_activation_plugin(PLUGIN_COLLISION_BOXES,
                                           wild9_collision_boxes_activate);
  (void)psx_mod_register_session_reset_plugin(
      PLUGIN_COLLISION_BOXES, wild9_collision_boxes_session_reset);
  /* Returns 0, so the flash pass still runs after the boxes are queued. */
  (void)psx_mod_register_function_override(
      PLUGIN_COLLISION_BOXES, W9_FRAME_HOOK_FUNC, wild9_collision_boxes_frame,
      frame_hook_entry, 2, 0);
}
