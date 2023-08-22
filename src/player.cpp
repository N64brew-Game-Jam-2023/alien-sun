#include "player.h"
#include "assets.h"
#include "map.h"
#include "cache.h"
#include "misc.h"

#include <box2d/box2d.h>
#include <stdlib.h>

#define JUMP_THRUST 30.f

#define RUN_FORCE 2.f
#define RUN_ACCEL (RUN_FORCE*0.5f)

#define SUB_FORCE 3.f
#define SUB_ACCEL (SUB_FORCE*0.3f)

#define YELLOW_MAX_HEALTH   100
#define YELLOW_SLIDE_LEN    15
#define YELLOW_SLIDE_THRUST 30.f

#define F_YELLOW_STAND   0
#define F_YELLOW_JUMP    1
#define F_YELLOW_FALL    2
#define F_YELLOW_WALK1   3
#define F_YELLOW_WALK6   8
#define F_YELLOW_SLIDE   9
#define F_YELLOW_CROUCH1 10
#define F_YELLOW_CROUCH2 11
#define F_YELLOW_LADDER1 12
#define F_YELLOW_LADDER4 15
#define F_YELLOW_HURT1   16
#define F_YELLOW_HURT2   17
#define F_YELLOW_SPIN1   18
#define F_YELLOW_SPIN8   25

static void yellow_tick(map_t *map, actor_t *actor);
static void yellow_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed);

static void submarine_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed);

void player_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  player_t *player = (player_t *) actor;
  mob_init(map, actor, spawn);
  if (actor->type == AT_YELLOW)
    player->mob.maxhealth = YELLOW_MAX_HEALTH;
  else
    player->mob.maxhealth = 1;
  player->mob.health = player->mob.maxhealth;
}

void player_tick(map_t *map, actor_t *actor) {
  if (actor->type == AT_YELLOW)
    mob_tick_subclass(map, actor, yellow_tick);
  else
    mob_tick(map, actor);
}

static void player_change(map_t *map, actor_t *newplayer) {
  //b2Vec2 cam = { map->camera_x, map->camera_y };
  //b2Vec2 pvec;

  //world_get_actor_position(newplayer, &pvec.x, &pvec.y);

  if (map->player)
    map->player->flags &= ~AF_CUR_PLAYER;
  newplayer->flags |= AF_CUR_PLAYER;
  map->player = newplayer;
  map->camera_target = newplayer;
  //map->state_flags |= MSF_CAMERA_MOVING|MSF_PLAYER_CHANGED;
  //map->camera_vel = CAMERA_ACCEL * 0.5f * (-sqrtf(1.f + 8.f * b2Distance(cam, pvec)) - 1.f);
  //map->camera_target_vel = 0.f;
}

void player_damage(map_t *map, actor_t *actor, int32_t damage, damage_source_t source) {
  mob_t *mob = (mob_t *) actor;
  if (damage > 0) {
    mob->health -= damage;
    if (mob->health <= 0) {
      mob->sprite.anim.frame = F_YELLOW_HURT2;
      mob->sprite.anim.counter = 0;
      actor->flags |= AF_NOCOLLIDE;
      if (map->camera_target == actor)
        map->camera_target = NULL;
      if (map->player == actor) {
        map->state_flags |= MSF_RESPAWNING;
        map->respawn_counter = 30;
      }
    } else if (source == DS_PHYSICAL) {
      uint32_t frame = mob->sprite.anim.frame;
      if (frame != F_YELLOW_HURT1 && frame != F_YELLOW_HURT2) {
        mob->sprite.anim.frame = F_YELLOW_HURT1;
        mob->sprite.anim.counter = 0;
      }
    }
  } else if (damage < 0) {
    //mob->health = MAX(mob->health - damage, mob->maxhealth);
    mob->health = mob->health - damage;
  }
}

bool player_respawn(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  player_t *player = (player_t *) actor;
  mob_t *mob = (mob_t *) actor;
  actor->flags &= ~AF_NOCOLLIDE;
  actor->body->SetType(b2_dynamicBody);
  actor_sprite_set_frame(map, actor, F_YELLOW_STAND);
  world_set_actor_transform(actor, spawn->x, spawn->y, ang16_to_radians(spawn->rotation));
  actor->body->SetLinearVelocity(b2Vec2(0, 0));
  actor->body->SetAngularVelocity(0);
  mob->health = YELLOW_MAX_HEALTH;
  mob->ground_count = 0;
  player->crystals = 0;
  if (map->player == actor)
    map->camera_target = actor;
  return true;
}

playersave_t player_highscore = { 0 };

void player_save(actor_t *actor, playersave_t *save) {
  save->crystals = 0;
  save->health = 0;
  if (actor->type == AT_YELLOW) {
    player_t *player = (player_t *) actor;
    save->crystals = player->crystals;
    save->health = player->mob.health;
    player_highscore = *save;
  }
}

void player_restore(actor_t *actor, const playersave_t *save) {
  if (actor->type == AT_YELLOW) {
    player_t *player = (player_t *) actor;
    player->crystals = save->crystals;
    player->mob.health = save->health;
  }
}

void player_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed) {
#ifndef NDEBUG
  if (kdown->c[0].L) {
    map->cheats ^= MC_DEBUG_DRAW;
  }
#endif
  if (actor->type == AT_YELLOW)
    yellow_movement(map, actor, kdown, kpressed);
  else if (actor->type == AT_SUBMARINE)
    submarine_movement(map, actor, kdown, kpressed);
}

void player_collide(actor_t *actor, fixture_t *fix_a, actor_t *other, fixture_t *fix_b, contact_t *con, const manifold_t *old) {
  mob_t *mob = (mob_t *) actor;
  if (!old && !fix_b->IsSensor()) {
    if (fix_a->GetUserData().id == TAG("foot")) {
      if (con->IsTouching())
        mob->ground_count++;
      else if (mob->ground_count > 0)
        mob->ground_count--;
    }
  }
}

void yellow_tick(map_t *map, actor_t *actor) {
  player_t *player = (player_t *) actor;
  mob_t *mob = (mob_t *) actor;
  auto vel = actor->body->GetLinearVelocity();

  if (mob->health > 0) {
    auto anim = &mob->sprite.anim;
    uint32_t frame = anim->frame;
    if (frame == F_YELLOW_SLIDE) {
      if (mob->counter) {
        mob->counter--;
      } else {
        frame = F_YELLOW_STAND;
        if (mob->ground_count)
          actor->body->SetLinearVelocity(b2Vec2(0, 0));
      }
    }
    if (frame != F_YELLOW_SLIDE) {
      if (mob->ground_count) {
        float absxvel = fabsf(vel.x);
        if (absxvel > 2.f) {
          if (frame < F_YELLOW_WALK1 || frame > F_YELLOW_WALK6)
            frame = F_YELLOW_WALK1;
          anim->speed = sqrtf(absxvel * 2.f);
        } else {
          frame = F_YELLOW_STAND;
        }
      } else if (frame != F_YELLOW_HURT1 && frame != F_YELLOW_HURT2) {
        if (vel.y < 0) {
          frame = F_YELLOW_JUMP;
        } else {
          frame = F_YELLOW_FALL;
        }
      }
    }
    if (actor_sprite_tick_frame(map, actor, frame))
      mob->ground_count = 0;
  }
  if (mob->health > 0 && (actor->flags & AF_UNDERWATER)) {
    if (player->breath_counter)
      player->breath_counter--;
    if (player->breath_counter == 0) {
      particle_spawn_t spawn = {};
      spawn.flags = PARTICLE_LAYER_1;
      spawn.gfx = GFX_BUBBLE;
      spawn.tiles = TILESET_BUBBLE;
      spawn.angle = 0xc000;
      spawn.angle_variance = 0x0100;
      spawn.animspeed = 0x100;
      spawn.animspeed_variance = 1;
      spawn.speed_variance = 2;
      spawn.speed = 0x200;
      spawn.count = 1;
      float x, y;
      world_get_actor_center(actor, &x, &y);
      x += (actor->flags & AF_FLIPX) ? -4 : 4;
      y -= 6;
      particle_spawn(map, x, y, &spawn);
    }
  }
  if (player->breath_counter == 0)
    player->breath_counter = RANDN(&map->rng, 20) + 40;
}

static void yellow_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed) {
  mob_t *mob = (mob_t *) actor;
  b2Body *body = actor->body;
  b2ContactEdge *edge = body->GetContactList();
  float x, y;

  if (mob->health <= 0)
    return;

  if (kdown->c[0].B) {
    while (edge) {
      if (edge->other->GetUserData().type == BODY_ACTOR) {
        actor_t *other = edge->other->GetUserData().actor;
        if (other->type == AT_TRIGGER) {
          if (other->flags & TRIG_MANUAL)
            trigger_activate(map, (trigger_t *) other, actor);
        } else if (actor->type == AT_YELLOW && other->type == AT_SUBMARINE) {
          auto fixture = contact_get_other_fixture(edge->contact, body);
          if (fixture->GetUserData().id == TAG("entr")) {
            submarine_t *sub = (submarine_t *) other;
            sub->inside = actor;
            actor->body->SetEnabled(false);
            mob->ground_count = 0;
            player_change(map, other);
            break;
          }
        }
      }
      edge = edge->next;
    }
  }

  world_get_actor_position(actor, &x, &y);
  bool underwater = y + 10 > map->water_line;
  bool jumped = false;
  if (kdown->c[0].A && (mob->ground_count || (underwater && body->GetLinearVelocity().y > -8.f))) {
    auto jumpVec = -JUMP_THRUST * b2Vec2(map->gravity_norm_x, map->gravity_norm_y);
    if (underwater) {
      jumpVec = 0.7 * jumpVec;
      actor_play_fx(actor, SFX_SWIM, 10);
    } else {
      actor_play_fx(actor, SFX_JUMP, 10);
    }
    body->ApplyLinearImpulseToCenter(jumpVec, true);
    if (actor_sprite_set_frame(map, actor, F_YELLOW_JUMP))
      mob->ground_count = 0;
    jumped = true;
  }
  int xstick = kpressed->c[0].x;
  if (abs(xstick) > JOYSTICK_DEAD_ZONE) {
    auto vel = body->GetLinearVelocity();
    float target = (xstick - SIGN(xstick) * (float) JOYSTICK_DEAD_ZONE) * (RUN_FORCE / JOYSTICK_DEAD_ZONE);
    if (target > 0 && vel.x < target) {
      vel.x = MIN(target, vel.x + RUN_ACCEL);
      body->SetLinearVelocity(vel);
    } else if (target < 0 && vel.x > target) {
      vel.x = MAX(target, vel.x - RUN_ACCEL);
      body->SetLinearVelocity(vel);
    }
  }
  int ystick = kpressed->c[0].y;
  if (!jumped && kdown->c[0].B && mob->ground_count && ystick < -50
      && mob->sprite.anim.frame != F_YELLOW_SLIDE) {
    mob->counter = YELLOW_SLIDE_LEN;
    b2Vec2 thrust = { YELLOW_SLIDE_THRUST, 0 };
    if (actor->flags & AF_FLIPX)
      thrust = -thrust;
    body->ApplyLinearImpulseToCenter(thrust, true);
    if (actor_sprite_set_frame(map, actor, F_YELLOW_SLIDE))
      mob->ground_count = 0;
  }
}

void spaceship_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_model_init(map, actor, spawn);
  spaceship_t *ship = (spaceship_t *) actor;
  ship->model.list = model_pool_load(MODEL_SPACESHIP, 1);
  ship->thrust.image = sprite_pool_load_gl(GFX_THRUST, &ship->thrust_tex);
  ship->thrust.tiles = tileset_pool_load(TILESET_THRUST);
  ship->thrust_list = glGenLists(1);
  glNewList(ship->thrust_list, GL_COMPILE);
  glBegin(GL_QUADS);
  int32_t nwidth = -ship->thrust.image->width;
  int32_t halfheight = ship->thrust.image->height >> 1;
  glVertex3i(nwidth, -halfheight, 0);
  glVertex3i(0, -halfheight, 0);
  glVertex3i(0, halfheight, 0);
  glVertex3i(nwidth, -halfheight, 0);
  glEnd();
  glEndList();
}

void spaceship_tick(map_t *map, actor_t *actor) {
}

void spaceship_cleanup(actor_t *actor) {
  actor_model_cleanup(actor);
  spaceship_t *ship = (spaceship_t *) actor;
  sprite_anim_cleanup(&ship->thrust);
  glDeleteLists(ship->thrust_list, 1);
}

void submarine_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_model_init(map, actor, spawn);
  submarine_t *sub = (submarine_t *) actor;
  sub->tiles = tileset_pool_load(actor->cls->tiles_id);
  actor->collision = tiles_get_collision(sub->tiles, 0);
  sub->model.list = model_pool_load(MODEL_SUB, 2);
  sub->propeller_tex = sprite_pool_load_gl(GFX_PROPELLER, &sub->propeller_gltex);
  if (spawn->flags & AF_FLIPX)
    sub->target_yaw = sub->user_yaw = -90;
  else
    sub->target_yaw = sub->user_yaw = 90;
}

void submarine_tick(map_t *map, actor_t *actor) {
  submarine_t *sub = (submarine_t *) actor;
  float x, y;

  world_get_actor_position(actor, &x, &y);
  if (y - 40 > map->water_line) {
    float pitch = sinf((sub->counter % 150) * ((M_PI*2.f)/150.f)) * 3.f;
    actor->body->ApplyAngularImpulse(pitch, true);
    sub->model.roll = sinf((sub->counter % 233) * ((M_PI*2.f)/233.f)) * 2.f;
    sub->model.yaw = sub->user_yaw + sinf((sub->counter % 407) * ((M_PI*2.f)/407.f)) * 4.f;
    sub->counter += 1;
    if (sub->counter >= (233*150*407))
      sub->counter = 0;
    sub->propeller_spin += sub->thrust;
    if (sub->propeller_spin >= 360.f)
      sub->propeller_spin -= 360.f;
    if (actor->body->GetLinearVelocity().y > -POINT_SCALE)
      actor->body->ApplyLinearImpulseToCenter(b2Vec2(0, -280.f), true);
  }

  if (sub->inside) {
    if (sub->inside->flags & AF_DESTROYING)
      sub->inside = NULL;
    else
      sub->inside->body->SetTransform(actor->body->GetPosition(), actor->body->GetAngle());
  }
}

static void submarine_movement(map_t *map, actor_t *actor, const pad_t *kdown, const pad_t *kpressed) {
  b2Body *body = actor->body;
  submarine_t *sub = (submarine_t *) actor;
  float x, y;

  world_get_actor_position(actor, &x, &y);
  if (kdown->c[0].A && sub->inside) {
    actor_t *player = sub->inside;
    sub->inside = NULL;
    player->body->SetEnabled(true);
    world_set_actor_transform(player, x - 10, y - 70, 0);
    player_change(map, player);
  }
  if (y - 40 > map->water_line) {
    int ystick = kpressed->c[0].y;
    if (abs(ystick) > JOYSTICK_DEAD_ZONE) {
      float pitchtarget = ystick > 0 ? -M_PI*0.25 : M_PI*0.25;
      float pitch = body->GetAngle();
      if (pitchtarget > pitch)
        body->ApplyAngularImpulse(10.f, true);
      else if (pitchtarget < pitch)
        body->ApplyAngularImpulse(-10.f, true);
    }
  }
  sub->thrust = 0.f;
  if (kpressed->c[0].B)
    sub->thrust += 4.f;
  if (kpressed->c[0].Z)
    sub->thrust -= 4.f;
  if (F2I(sub->thrust) != 0) {
    auto normal = body->GetTransform().q.GetXAxis();
    auto prop = body->GetWorldCenter() - 40.f * normal;
    body->ApplyLinearImpulse(sub->thrust * 8.f * normal, prop, true);
  }
}

void submarine_cleanup(actor_t *actor) {
  submarine_t *sub = (submarine_t *) actor;
  sprite_pool_unload(sub->propeller_tex);
  tileset_pool_unload(sub->tiles);
  actor_model_cleanup(actor);
}

