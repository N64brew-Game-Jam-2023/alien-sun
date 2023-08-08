#include "enemy.h"
#include "assets.h"

#include <box2d/box2d.h>

void mob_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_sprite_init(map, actor, spawn);
  mob_t *mob = (mob_t *) actor;
  mob->last_flags = actor->flags;
}

void mob_tick(map_t *map, actor_t *actor) {
  mob_tick_subclass(map, actor, NULL);
}

void mob_tick_subclass(map_t *map, actor_t *actor, actor_ticker_t ticker) {
  mob_t *mob = (mob_t *) actor;

  auto vel = actor->body->GetLinearVelocity();

  if (vel.x > POINT_SCALE)
    actor->flags &= ~AF_FLIPX;
  else if (vel.x < -POINT_SCALE)
    actor->flags |= AF_FLIPX;

  if (ticker)
    ticker(map, actor);

  if (mob->target && (mob->target->flags & AF_DESTROYING))
    mob->target = NULL;

  if (actor->flags != mob->last_flags) {
    world_update_actor_state(actor);
    mob->last_flags = actor->flags;
  }
}

void mine_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_sprite_init(map, actor, spawn);
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  sprite->anim.speed = RANDF(&map->rng) * 0.25f + 0.5f;
  sprite->anim.counter = M_PI*2.f * RANDF(&map->rng);
}

void mine_tick(map_t *map, actor_t *actor) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  sprite->anim.counter += M_PI*2.f*(1.f/150.f);
  if (sprite->anim.counter >= M_PI*2.f)
    sprite->anim.counter -= M_PI*2.f;
  world_set_actor_angle(actor, sinf(sprite->anim.counter) * 0.2f);
}

void mine_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  if (!old && con->IsTouching() && other) {
    map_t *map = world_body_get_map(actor->body);
    particle_spawn_t spawn = { .flags = PARTICLE_LAYER_1, .animspeed = 0x400 };
    int damage = 1;
    if (actor->type == AT_MINE_BIG) {
      spawn.gfx = GFX_WATER_EXPL_BIG;
      spawn.tiles = TILESET_WATER_EXPL_BIG;
      damage = 100;
    } else if (actor->type == AT_MINE_MED) {
      spawn.gfx = GFX_WATER_EXPL_MID;
      spawn.tiles = TILESET_WATER_EXPL_MID;
      damage = 50;
    } else if (actor->type == AT_MINE_SM) {
      spawn.gfx = GFX_WATER_EXPL_SM;
      spawn.tiles = TILESET_WATER_EXPL_SM;
      damage = 25;
    }
    if (other->cls->damage)
      other->cls->damage(map, other, damage, DS_PHYSICAL);

    b2WorldManifold manifold;
    con->GetWorldManifold(&manifold);

    float x, y;
    world_get_actor_center(actor, &x, &y);
    particle_spawn(map, x, y, &spawn);

    spawn.gfx = GFX_BUBBLES;
    spawn.tiles = TILESET_BUBBLES;
    spawn.width_variance = damage >> 1;
    spawn.height_variance = damage >> 1;
    spawn.angle = 0xc000;
    spawn.animspeed = 0xd0;
    spawn.animspeed_variance = 1;
    spawn.speed_variance = 2;
    spawn.speed = 0x180;
    spawn.count = damage >> 4;
    spawn.count_variance = damage >> 5;
    particle_spawn(map, x, y, &spawn);

    other->body->ApplyLinearImpulseToCenter(sqrtf(damage) * -8.f * manifold.normal, true);
    actor_destroy(map, actor);
  }
}
