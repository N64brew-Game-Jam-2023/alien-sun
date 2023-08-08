
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "actors.h"

typedef struct {
  actor_sprite_t sprite;
  uint16_t counter;
  uint16_t ground_count;
  int16_t health;
  int16_t maxhealth;
  uint32_t last_flags;
  actor_t *target;
} mob_t;

void mob_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void mob_tick(map_t *map, actor_t *actor);
void mob_tick_subclass(map_t *map, actor_t *actor, actor_ticker_t ticker);

void mine_init(map_t *map, actor_t *actor, actor_spawn_t *spawn);
void mine_tick(map_t *map, actor_t *actor);
void mine_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old);

#ifdef __cplusplus
}
#endif
