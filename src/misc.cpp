#include "misc.h"
#include "map.h"
#include "player.h"
#include "assets.h"

#include <box2d/box2d.h>
#include <cinttypes>

#define MUSHROOM_THRUST 42.f

void trigger_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  trigger_t *trigger = (trigger_t *) actor;
  trigger_arg_t *arg = (trigger_arg_t *) map_get_pointer(map, spawn->arg_int);
  trigger->script = arg->script;
  if (arg->collision >= (uintptr_t) KSEG0_START_ADDR) {
    actor->collision = (collision_t *) arg->collision;
    actor->flags |= AF_COLLISION_OWNED;
  } else {
    actor->collision = (collision_t *) map_get_pointer(map, arg->collision);
  }
}

void trigger_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  if (!old && con->IsTouching() && !(actor->flags & TRIG_MANUAL))
    trigger_activate(world_body_get_map(actor->body), (trigger_t *) actor, other);
}

void trigger_activate(map_t *map, trigger_t *trigger, actor_t *activator) {
  uint32_t cat = activator->cls->category_bits;
  if (cat == 0)
    return;
  if (!(trigger->actor.flags & cat & TRIG_CB_MASK))
    return;
  if ((trigger->actor.flags & TRIG_CUR_PLAYER) && (!(activator->flags & AF_CUR_PLAYER)))
    return;

  if (trigger->script >= (uintptr_t) KSEG0_START_ADDR)
    script_start_dynamic(map, (dynscript_t *) trigger->script, activator);
  else
    script_start(map, trigger->script, activator);

  if (!(trigger->actor.flags & TRIG_REPEATABLE))
    actor_destroy(map, &trigger->actor);
}

void trigger_cleanup(actor_t *actor) {
  trigger_t *trigger = (trigger_t *) actor;
  if (trigger->script >= (uintptr_t) KSEG0_START_ADDR)
    script_unref((dynscript_t *) trigger->script);
}

#define MUSHROOM_FLAG_BOUNCED AF_USER0

void mushroom_tick(map_t *map, actor_t *actor) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  if (actor->flags & MUSHROOM_FLAG_BOUNCED) {
    actor_sprite_set_frame(world_body_get_map(actor->body), actor, 1);
    actor->flags &= ~MUSHROOM_FLAG_BOUNCED;
  } else if (sprite->anim.frame > 0)
    sprite_anim_tick(&sprite->anim);
}

void mushroom_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  if (!old && other && con->IsTouching() && !(actor->flags & MUSHROOM_FLAG_BOUNCED)) {
    if (fix_b->GetUserData().id == TAG("foot")) {
      if (other->body->GetLinearVelocity().y > -2.f) {
        b2Vec2 thrust = b2Vec2(0.f, -MUSHROOM_THRUST);
        if (actor->type == AT_MUSH_SPRING_BIG)
          thrust = 2.f * thrust;
        other->body->ApplyLinearImpulseToCenter(thrust, true);
        actor->flags |= MUSHROOM_FLAG_BOUNCED;
      }
    }
  }
}

void crystal_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_sprite_init(map, actor, spawn);
  crystal_t *crystal = (crystal_t *) actor;
  crystal->y = spawn->y;
  crystal->speed = (RANDF(&map->rng) + 0.5f) * (1.0/(M_PI*2.f));
  crystal->counter = RANDF(&map->rng) * M_PI*2.f;
  crystal->sprite.anim.speed = RANDF(&map->rng) + 0.5f;
}

void crystal_tick(map_t *map, actor_t *actor) {
  crystal_t *crystal = (crystal_t *) actor;
  float x, y;

  crystal->counter += crystal->speed;
  world_get_actor_position(actor, &x, &y);
  y = crystal->y + sinf(crystal->counter) * 4.f;
  world_set_actor_position(actor, x, y);
}

void crystal_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  if (!old && con->IsTouching() && other && (other->cls->category_bits & CB_PLAYER)) {
    map_t *map = world_body_get_map(actor->body);
    player_t *player = (player_t *) other;
    if (actor->type == AT_CRYSTAL_BIG)
      player->crystals += 5;
    else if (actor->type == AT_CRYSTAL_SM)
      player->crystals++;
    if (map->hudplayer == other)
      map->hud_crystal_counter = 90;
    actor_destroy(map, actor);
  }
}

void powerup_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  if (!old && con->IsTouching() && other && (other->cls->category_bits & CB_PLAYER)) {
    map_t *map = world_body_get_map(actor->body);
    if (other->cls->damage)
      other->cls->damage(map, other, -10, DS_PHYSICAL);
    actor_destroy(map, actor);
  }
}

void particle_spawn_splash(map_t *map, float cx) {
  particle_spawn_t spawn = { .flags = PARTICLE_LAYER_1 };
  spawn.gfx = GFX_SPLASH;
  spawn.tiles = TILESET_SPLASH;
  particle_spawn(map, cx + 13, map->water_line, &spawn);
}

void crate_tick(map_t *map, actor_t *actor) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  float x, y;
  world_get_actor_position(actor, &x, &y);
  y += sprite->anim.image->height >> 2;
  if (y > map->water_line)
    actor->body->ApplyForceToCenter(b2Vec2(0,  (map->water_line - y) * 256.f), true);
}

void platform_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_sprite_init(map, actor, spawn);
  platform_t *platform = (platform_t *) actor;

  platform->init_x = spawn->x;
  platform->init_y = spawn->y;
  platform->step = -1.f;
  platform->center_x = FLT_MIN;
  platform->center_x = FLT_MIN;

  uint16_t waypoint = spawn->arg_int & 0xffff;
  uint16_t speed = spawn->arg_int >> 16;
  if (waypoint < map->header->waypoint_count) {
    actor_target_t target;
    target.is_waypoint = true;
    target.waypoint = &map->header->waypoints[waypoint];
    platform_set_target(map, actor, target);
  } else if (waypoint != 0xffff) {
    debugf("platform spawned with invalid waypoint %" PRIuPTR "\n", waypoint);
  }
  if (!speed)
    speed = 0x10;
  platform->speed = ((float) speed) * (1.f/16.f);
}

static inline float platform_flags_angle_scale(platform_flags_t type) {
  switch (type) {
    case PF_SWING_90:
      return 0.5f;
    case PF_SWING_45:
      return 0.25f;
    case PF_SWING_22:
    default:
      return 0.125f;
  }
}

void platform_tick(map_t *map, actor_t *actor) {
  platform_t *platform = (platform_t *) actor;
  platform_flags_t type = (platform_flags_t) (actor->flags & AF_USERMASK);
  waypoint_t *waypoint = platform->waypoint;
  if (waypoint) {
    float x = waypoint->x;
    float y = waypoint->y;
    float counter = platform->counter;

    if (platform->step < 0) {
      b2Vec2 pos;
      b2Vec2 way = { (float) waypoint->x, (float) waypoint->y };

      world_get_actor_position(actor, &pos.x, &pos.y);
      pos.x += platform->center_x;
      pos.y += platform->center_y;
      platform->dist = b2Distance(pos, way);
      if (platform->dist)
        platform->step = (1.0 / platform->dist) * platform->speed * 0.5f;
      else
        platform->step = 0.0;
    }

    switch (type) {
    case PF_LINEAR:
      x = (x - platform->init_x) * platform->counter + platform->init_x;
      y = (y - platform->init_y) * platform->counter + platform->init_y;
      break;
    case PF_HSINE:
      x = x + platform->dist * sinf(counter * M_PI * 2.f);
      break;
    case PF_VSINE:
      y = y + platform->dist * sinf(counter * M_PI * 2.f);
      break;
    case PF_CIRCLE_CW:
      x = x + platform->dist * cosf(counter * M_PI * 2.f);
      y = y + platform->dist * sinf(counter * M_PI * 2.f);
      break;
    case PF_CIRCLE_CCW:
      x = x + platform->dist * -cosf(counter * M_PI * 2.f);
      y = y + platform->dist * sinf(counter * M_PI * 2.f);
      break;
    case PF_SWING_90:
    case PF_SWING_45:
    case PF_SWING_22:
      {
        float swing = sinf(counter * M_PI * 2.f) * M_PI * platform_flags_angle_scale(type);
        x = x + platform->dist * sinf(swing);
        y = y + platform->dist * cosf(swing);
        world_set_actor_angle(actor, -swing);
      }
      break;
    default:
      return;
    }

    platform->counter += platform->step;
    if (platform->counter >= 1.f) {
      platform->counter = 0.f;
      if (type == PF_LINEAR) {
        platform->init_x = x = waypoint->x;
        platform->init_y = y = waypoint->y;
        platform->waypoint = waypoint->next;
      }
    }

    world_set_actor_position(actor, x, y);
  }
}

void platform_set_target(map_t *map, actor_t *actor, actor_target_t target) {
  platform_t *platform = (platform_t *) actor;
  if (target.is_waypoint && platform->waypoint != target.waypoint) {
    platform->waypoint = target.waypoint;
    platform->counter = 0;
    platform->step = -1.f;
  }
}
