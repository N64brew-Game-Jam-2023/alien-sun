#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "enemy.h"

typedef struct {
  mob_t mob;
  uint32_t crystals;
  uint32_t breath_counter;
} player_t;

typedef enum {
  SPACESHIP_THRUST_BOT  = AF_USER0,
  SPACESHIP_THRUST_BACK = AF_USER1,
} spaceship_flags_t;

typedef struct {
  actor_model_t model;
  sprite_anim_t thrust;
  GLuint thrust_tex;
  GLuint thrust_list;
  uint32_t flags;
} spaceship_t;

typedef struct {
  actor_model_t model;
  tiles_desc_t *tiles;
  sprite_t *propeller_tex;
  GLuint propeller_gltex;
  float propeller_spin;
  float thrust;
  float user_yaw;
  float target_yaw;
  float target_pitch;
  uint32_t counter;
  actor_t *inside;
} submarine_t;

typedef struct {
  uint32_t health;
  uint32_t crystals;
} playersave_t;

void player_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void player_tick(map_t *map, actor_t *actor);
void player_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed);
void player_damage(map_t *map, actor_t *actor, int32_t damage, damage_source_t source);
void player_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);
bool player_respawn(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void player_save(actor_t *actor, playersave_t *save);
void player_restore(actor_t *actor, const playersave_t *save);

extern playersave_t player_highscore;

void spaceship_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void spaceship_tick(map_t *map, actor_t *actor);
void spaceship_cleanup(actor_t *actor);

void submarine_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void submarine_tick(map_t *map, actor_t *actor);
void submarine_cleanup(actor_t *actor);

#ifdef __cplusplus
}
#endif
