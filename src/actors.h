#pragma once

#include <GL/gl.h>
#include <graphics.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/queue.h>

#include "actortypes.h"
#include "map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct actor_s actor_t;

typedef struct {
  bool is_waypoint;
  union {
    actor_t *actor;
    waypoint_t *waypoint;
  };
} actor_target_t;

typedef enum {
  DS_AMBIENT,
  DS_PHYSICAL,
} damage_source_t;

typedef void (*actor_init_t)(map_t *map, actor_t *, actor_spawn_t *);
typedef void (*actor_ticker_t)(map_t *, actor_t *);
typedef void (*actor_set_target_t)(map_t *, actor_t *, actor_target_t);
typedef void (*actor_collide_t)(actor_t *, fixture_t *, actor_t *, fixture_t *, contact_t *, const manifold_t *);
typedef void (*actor_damage_t)(map_t *, actor_t *, int32_t, damage_source_t);
typedef void (*actor_drawer_t)(actor_t *, const irect2_t *rect);
typedef void (*actor_cleanup_t)(actor_t *);

typedef struct {
  actor_init_t init;
  actor_ticker_t ticker;
  actor_set_target_t set_target;
  actor_collide_t collider;
  actor_damage_t damage;
  actor_drawer_t drawer;
  actor_cleanup_t cleanup;
  uint16_t gfx_id;
  uint16_t tiles_id;
  uint32_t flags;
  size_t struct_size;
  int16_t draw_priority;
  int16_t collide_priority;
  uint16_t category_bits;
  uint16_t category_mask;
  float density;
} actor_class_t;

struct actor_s {
  LIST_ENTRY(actor_s) map;
  LIST_HEAD(, script_state_s) callers;
  actor_ticker_t ticker;
  actor_drawer_t drawer;
  actor_collide_t collider;
  uint16_t type;
  uint16_t id;
  uint32_t flags;
  const actor_class_t *cls;
  collision_t *collision;
  body_t *body;
  uint32_t frame_drawn;
  int sound_channel;
};

typedef enum {
  AF_USER0           = 1 << 0,
  AF_USER1           = 1 << 1,
  AF_USER2           = 1 << 2,
  AF_USER3           = 1 << 3,
  AF_USER4           = 1 << 4,
  AF_USER5           = 1 << 5,
  AF_USER6           = 1 << 6,
  AF_USER7           = 1 << 7,
  AF_USER8           = 1 << 8,
  AF_USER9           = 1 << 9,
  AF_USER10          = 1 << 10,
  AF_USER11          = 1 << 11,
  AF_USER12          = 1 << 12,
  AF_USER13          = 1 << 13,
  AF_USER14          = 1 << 14,
  AF_USER15          = 1 << 15,

  AF_USERMASK        = 0xffff,

  AF_KINEMATIC       = 1 << 18,
  AF_UNDERWATER      = 1 << 19,
  AF_ROTATES         = 1 << 20,
  AF_NOCOLLIDE       = 1 << 21,
  AF_STATIC          = 1 << 22,
  AF_GRAVITY         = 1 << 23,
  AF_SOLID           = 1 << 24,
  AF_COLLISION_OWNED = 1 << 25,
  AF_FLIPD           = 1 << 26,
  AF_FLIPY           = 1 << 27,
  AF_FLIPX           = 1 << 28,
  AF_CUR_PLAYER      = 1 << 29,
  AF_DESTROYING      = 1 << 30,
  AF_DESTROYED       = 1 << 31,
} actor_flags_t;

extern actor_class_t actor_classes[NUM_ACTORS];
extern const size_t actor_class_count;

// actor subtypes

typedef struct {
  actor_t actor;
  float scale_x;
  float scale_y;
  sprite_anim_t anim;
} actor_sprite_t;

typedef struct {
  actor_t actor;
  float z;
  float yaw;
  float roll;
  float scale_x;
  float scale_y;
  float scale_z;
  sprite_t *tex;
  GLuint gltex;
  GLuint list;
} actor_model_t;

actor_t *actor_spawn(map_t *map, actor_spawn_t *spawn);
void actor_play_fx(actor_t *actor, uint16_t sound_id, int priority);
void actor_destroy(map_t *map, actor_t *actor);
void actor_finalize(actor_t *actor);

collision_t *tiles_get_collision(tiles_desc_t *tiles, uint16_t frame);
bool sprite_in_rect(actor_sprite_t *sprite, const irect2_t *rect);

void actor_sprite_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
bool actor_sprite_set_frame(map_t *map, actor_t *actor, uint16_t frame);
bool actor_sprite_tick_frame(map_t *map, actor_t *actor, uint16_t frame);
void actor_sprite_cleanup(actor_t *actor);

void actor_model_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void actor_model_cleanup(actor_t *actor);

#ifdef __cplusplus
}
#endif
