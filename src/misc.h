#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "actors.h"

typedef enum {
  TRIG_PLAYER      = CB_PLAYER,
  TRIG_ENEMY       = CB_ENEMY,
  TRIG_PROP        = CB_PROP,
  TRIG_PROJECTILE  = CB_PROJECTILE,
  TRIG_CB_MASK     = (CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE),
  TRIG_REPEATABLE  = AF_USER8,
  TRIG_MANUAL      = AF_USER9,
  TRIG_CUR_PLAYER  = AF_USER10,
} trigger_flags_t;

typedef struct {
  actor_t actor;
  uintptr_t script;
} trigger_t;

typedef struct {
  uintptr_t script;
  uintptr_t collision;
} trigger_arg_t;

typedef struct {
  actor_sprite_t sprite;
  float counter;
  float speed;
  float y;
} crystal_t;

typedef struct {
  actor_sprite_t sprite;
  waypoint_t *waypoint;
  float speed;
  float dist;
  float step;
  float counter;
  float init_x;
  float init_y;
  float center_x;
  float center_y;
} platform_t;

typedef enum {
  PF_LINEAR = 0,
  PF_HSINE = 1,
  PF_VSINE = 2,
  PF_CIRCLE_CW = 3,
  PF_CIRCLE_CCW = 4,
  PF_SWING_90 = 5,
  PF_SWING_45 = 6,
  PF_SWING_22 = 7,
} platform_flags_t;

void trigger_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void trigger_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);
void trigger_activate(map_t *map, trigger_t *trigger, actor_t *activator);
void trigger_cleanup(actor_t *actor);

void mushroom_tick(map_t *map, actor_t *actor);
void mushroom_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);

void crystal_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void crystal_tick(map_t *map, actor_t *actor);
void crystal_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);

void powerup_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);

void particle_spawn_splash(map_t *map, float cx);

void crate_tick(map_t *map, actor_t *actor);

void platform_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void platform_tick(map_t *map, actor_t *actor);
void platform_set_target(map_t *map, actor_t *actor, actor_target_t target);

#ifdef __cplusplus
}
#endif
