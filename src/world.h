#pragma once

#include "util.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INV_POINT_SCALE (16.f)
#define POINT_SCALE (1.0f/INV_POINT_SCALE)

#define CB_GROUND       (1<<0)
#define CB_PLAYER       (1<<1)
#define CB_ENEMY        (1<<2)
#define CB_PROP         (1<<3)
#define CB_PROJECTILE   (1<<4)
#define CB_WATER        (1<<5)
#define CB_POWERUP      (1<<6)
#define CB_TRIGGER      (1<<14)
#define CB_INTERACTIVE  (1<<15)
#define CB_ALL          0xffff

typedef struct map_s map_t;
typedef struct actor_s actor_t;
typedef struct tile_chunk_s tile_chunk_t;

typedef struct world_s world_t;
typedef struct b2Body body_t;
typedef struct b2Contact contact_t;
typedef struct b2Fixture fixture_t;
typedef struct b2Manifold manifold_t;

typedef struct {
  uint16_t type;
  uint16_t flags;
  uint32_t id;
} collision_t;

typedef uint64_t world_position_t;

union world_position_u {
  uint64_t i;
  struct { float x, y; };
};

world_t *world_new(map_t *map, float gravity_x, float gravity_y, float water_y);
void world_tick(world_t *world);
void world_destroy(world_t *world);

typedef bool (*world_actor_func_t)(actor_t *, const irect2_t *rect, void *);

void world_foreach_actor_in_rect(world_t *world, const irect2_t *rect, int32_t expand, world_actor_func_t func, void *arg);

void world_link_actor(world_t *world, actor_t *actor, float x, float y, float angle);

world_position_t _world_get_actor_position(actor_t *actor);
world_position_t _world_get_actor_center(actor_t *actor);
float world_get_actor_angle(actor_t *actor);
void world_set_actor_position(actor_t *actor, float x, float y);
void world_set_actor_position_centered(actor_t *actor, float x, float y);
void world_set_actor_angle(actor_t *actor, float angle);
void world_set_actor_transform(actor_t *actor, float x, float y, float angle);

void world_update_actor_state(actor_t *actor);
void world_update_actor_collision(actor_t *actor);

void world_move_water(world_t *world, float y);
void world_set_gravity(world_t *world, float gravity_x, float gravity_y);

map_t *world_body_get_map(body_t *body);
void world_body_destroy(body_t *body);

#define world_get_actor_position(_actor, _x, _y) do { \
    union world_position_u u; \
    u.i = _world_get_actor_position((_actor)); \
    *(_x) = u.x; \
    *(_y) = u.y; \
  } while (0)

#define world_get_actor_center(_actor, _x, _y) do { \
    union world_position_u u; \
    u.i = _world_get_actor_center((_actor)); \
    *(_x) = u.x; \
    *(_y) = u.y; \
  } while (0)

#ifdef NDEBUG
#define world_debug_draw(world) ((void)world)
#else
void world_debug_draw(world_t *world);
#endif

fixture_t *contact_get_other_fixture(contact_t *contact, body_t *body);

#ifdef __cplusplus
}
#endif
