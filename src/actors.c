#include <fmath.h>
#include <inttypes.h>
#include <malloc.h>

#include "actors.h"
#include "assets.h"
#include "cache.h"
#include "misc.h"
#include "player.h"
#include "render.h"
#include "sound.h"
#include "util.h"

#define DPRIORITY_PLAYER 100
#define DPRIORITY_ENEMY 75
#define DPRIORITY_POWERUP 50
#define DPRIORITY_PROP 25

#define CPRIORITY_TRIGGER 100
#define CPRIORITY_ENEMY 75
#define CPRIORITY_POWERUP 50
#define CPRIORITY_PLAYER 25

actor_t *actor_spawn(map_t *map, actor_spawn_t *spawn) {
  assertf(spawn->type < actor_class_count, "invalid actor type %"PRIu32, spawn->type);
  const actor_class_t *cls = &actor_classes[spawn->type];
  assertf(cls->struct_size > 0, "class for actor id %"PRIu32" has no struct size", spawn->type);
  actor_t *actor = calloc(1, cls->struct_size);
  assertf(actor != NULL, "out of memory");
  LIST_INSERT_HEAD(&map->actors, actor, map);
  actor->flags = cls->flags | spawn->flags;
  actor->id = spawn->id;
  actor->type = spawn->type;
  actor->cls = cls;
  actor->ticker = cls->ticker;
  actor->drawer = cls->drawer;
  actor->collider = cls->collider;
  actor->sound_channel = -1;
  LIST_INIT(&actor->callers);
  if (cls->init)
    cls->init(map, actor, spawn);
  world_link_actor(map->world, actor, spawn->x, spawn->y, ang16_to_radians(spawn->rotation));
  if (actor->flags & AF_CUR_PLAYER) {
    map->player = actor;
    map->camera_target = actor;
    if (!map->hudplayer)
      map->hudplayer = actor;
  }
  return actor;
}

void actor_play_fx(actor_t *actor, uint16_t sound_id, int priority) {
  float x, y;
  world_get_actor_position(actor, &x, &y);
  sound_play_fx(sound_id, x, y, priority, &actor->sound_channel);
}

void actor_destroy(map_t *map, actor_t *actor) {
  if (actor->flags & AF_DESTROYING)
    return;
  actor->flags |= AF_DESTROYING;
  if (actor->flags & AF_COLLISION_OWNED)
    free(actor->collision);
  sound_release_channel(&actor->sound_channel);
  actor->ticker = NULL;
  actor->drawer = NULL;
  actor->collider = NULL;
  LIST_REMOVE(actor, map);
  LIST_INSERT_HEAD(&map->dead, actor, map);
  script_state_t *state, *next;
  LIST_FOREACH_SAFE(state, &actor->callers, caller_entry, next) {
    state->caller = NULL;
    LIST_REMOVE(state, caller_entry);
  }
  if (map->player == actor)
    map->player = NULL;
  if (map->hudplayer == actor) {
    map->hudplayer = NULL;
    map->hud_crystal_counter = 0;
  }
  if (map->camera_target == actor)
    map->camera_target = NULL;
  if (map->dialog_target == actor)
    map->dialog_target = NULL;
}

void actor_finalize(actor_t *actor) {
  if (actor->cls->cleanup)
    actor->cls->cleanup(actor);
  if (actor->body)
    world_body_destroy(actor->body);
  LIST_REMOVE(actor, map);
  free(actor);
}

bool sprite_in_rect(actor_sprite_t *sprite, const irect2_t *rect) {
  int32_t width = sprite->anim.image->width;
  int32_t height = sprite->anim.tiles->frame_height;
  int32_t offset_x = sprite->anim.tiles->offset_x;
  int32_t offset_y = sprite->anim.tiles->offset_y;

  if (sprite->actor.flags & AF_FLIPX)
    offset_x = -offset_x;
  if (sprite->actor.flags & AF_FLIPY)
    offset_y = -offset_y;
  if (sprite->actor.flags & AF_FLIPD) {
    SWAP(width, height);
    SWAP(offset_x, offset_y);
  }

  float x, y;
  world_get_actor_position(&sprite->actor, &x, &y);
  float angle = world_get_actor_angle(&sprite->actor);
  if (sprite->scale_x != 1.0 || sprite->scale_y != 1.0 || angle) {
    float x0 = x + offset_x;
    if (sprite->scale_x != 1.0) {
      int32_t hwidth = width >> 1;
      x0 = x0 + hwidth - hwidth * sprite->scale_x;
      width *= sprite->scale_x;
    }
    if (angle)
      width *= M_SQRT2;
    if (x0 >= rect->x1)
      return false;
    if (x0 + width < rect->x0)
      return false;

    float y0 = y + offset_y;
    if (sprite->scale_y != 1.0) {
      int32_t hheight = height >> 1;
      y0 = y0 + hheight - hheight * sprite->scale_y;
      height *= sprite->scale_y;
    }
    if (angle)
      height *= M_SQRT2;
    if (y0 >= rect->y1)
      return false;
    if (y0 + height < rect->y0)
      return false;

  } else {

    int32_t x0 = x + offset_x;
    if (x0 >= rect->x1)
      return false;
    if (x0 + width < rect->x0)
      return false;
    int32_t y0 = y + offset_y;
    if (y0 >= rect->y1)
      return false;
    if (y0 + height < rect->y0)
      return false;

  }

  return true;
}

collision_t *tiles_get_collision(tiles_desc_t *tiles, uint16_t frame) {
  assertf(frame < tiles->frame_count, "invalid tile frame %d (max %d)", frame, tiles->frame_count);
  size_t offset = tiles->frames[frame].collision_offset;
  return (collision_t *) &((uint8_t *) tiles)[offset];
}

void actor_sprite_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  sprite->anim.speed = 1.0;
  sprite->scale_x = 1.0;
  sprite->scale_y = 1.0;
  if (actor->cls->gfx_id)
    sprite->anim.image = sprite_pool_load(actor->cls->gfx_id);
  if (actor->cls->tiles_id) {
    tiles_desc_t *tiles = tileset_pool_load(actor->cls->tiles_id);
    sprite->anim.tiles = tiles;
    if (tiles) {
      spawn->x -= tiles->offset_x;
      spawn->y -= tiles->offset_y;
      if (tiles->frame_count)
        sprite->actor.collision = tiles_get_collision(tiles, sprite->anim.frame);
    }
  }
}

static void actor_sprite_tick(map_t *map, actor_t *actor) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  collision_t *old_collision = actor->collision;
  if (sprite_anim_tick(&sprite->anim) || old_collision != actor->collision)
    world_update_actor_collision(actor);
}

bool actor_sprite_set_frame(map_t *map, actor_t *actor, uint16_t frame) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  collision_t *old_collision = actor->collision;
  collision_t *collision = tiles_get_collision(sprite->anim.tiles, frame);
  sprite->anim.frame = frame;
  sprite->anim.counter = 0;
  if (collision != old_collision) {
    actor->collision = collision;
    world_update_actor_collision(actor);
    return true;
  }
  return false;
}

bool actor_sprite_tick_frame(map_t *map, actor_t *actor, uint16_t frame) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  if (frame != sprite->anim.frame)
    return actor_sprite_set_frame(map, actor, frame);
  collision_t *old_collision = actor->collision;
  actor_sprite_tick(map, actor);
  return actor->collision != old_collision;
}

void actor_sprite_cleanup(actor_t *actor) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  sprite_anim_cleanup(&sprite->anim);
}

void actor_model_init(map_t *map, actor_t *actor, actor_spawn_t *spawn) {
  actor_model_t *model = (actor_model_t *) actor;
  if (actor->cls->gfx_id)
    model->tex = sprite_pool_load_gl(actor->cls->gfx_id, &model->gltex);
  model->scale_x = model->scale_y = model->scale_z = 1.0;
}

void actor_model_cleanup(actor_t *actor) {
  actor_model_t *model = (actor_model_t *) actor;
  if (model->tex)
    sprite_pool_unload(model->tex);
  if (model->list)
    model_pool_unload(model->list);
}

actor_class_t actor_classes[] = {
  [AT_TRIGGER] = {
    .init = trigger_init,
    .collider = trigger_collide,
    .cleanup = trigger_cleanup,
    .struct_size = sizeof(trigger_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_TRIGGER,
    .category_bits = CB_TRIGGER,
    .category_mask = CB_PLAYER|CB_PROP|CB_ENEMY|CB_PROJECTILE,
  },
  [AT_YELLOW] = {
    .gfx_id = GFX_YELLOW,
    .tiles_id = TILESET_YELLOW,
    .init = player_init,
    .ticker = player_tick,
    .collider = player_collide,
    .damage = player_damage,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(player_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_PLAYER,
    .draw_priority = DPRIORITY_PLAYER,
    .category_bits = CB_PLAYER,
    .density = 1.f,
  },
  [AT_PINK] = {
    .gfx_id = GFX_PINK,
    .tiles_id = TILESET_PINK,
    .init = player_init,
    .ticker = player_tick,
    .collider = player_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(player_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_PLAYER,
    .draw_priority = DPRIORITY_PLAYER,
    .category_bits = CB_PLAYER,
    .density = 1.f,
  },
  [AT_BLUE] = {
    .gfx_id = GFX_BLUE,
    .tiles_id = TILESET_BLUE,
    .init = player_init,
    .ticker = player_tick,
    .collider = player_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(player_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_PLAYER,
    .draw_priority = DPRIORITY_PLAYER,
    .category_bits = CB_PLAYER,
    .density = 1.f,
  },
  [AT_CRATE] = {
    .gfx_id = GFX_CRATE,
    .tiles_id = TILESET_CRATE,
    .init = actor_sprite_init,
    .ticker = crate_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY | AF_ROTATES,
    .draw_priority = DPRIORITY_PROP,
    .category_bits = CB_PROP,
    .density = 0.6f,
  },
  [AT_CRATE_BIG] = {
    .gfx_id = GFX_CRATE_BIG,
    .tiles_id = TILESET_CRATE_BIG,
    .init = actor_sprite_init,
    .ticker = crate_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY | AF_ROTATES,
    .draw_priority = DPRIORITY_PROP,
    .category_bits = CB_PROP,
    .density = 1.2f,
  },
  [AT_MUSH_SPRING_SM] = {
    .gfx_id = GFX_MUSH_SPRING_SM,
    .tiles_id = TILESET_MUSH_SPRING_SM,
    .init = actor_sprite_init,
    .ticker = mushroom_tick,
    .collider = mushroom_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_TRIGGER,
    .category_bits = CB_INTERACTIVE,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP,
  },
  [AT_MUSH_SPRING_BIG] = {
    .gfx_id = GFX_MUSH_SPRING_BIG,
    .tiles_id = TILESET_MUSH_SPRING_BIG,
    .init = actor_sprite_init,
    .ticker = mushroom_tick,
    .collider = mushroom_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_TRIGGER,
    .category_bits = CB_INTERACTIVE,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP,
  },
  [AT_WALKER] = {
    .gfx_id = GFX_WALKER,
    .tiles_id = TILESET_WALKER,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 5.f,
  },
  [AT_CRAB] = {
    .gfx_id = GFX_CRAB,
    .tiles_id = TILESET_CRAB,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_PIRANHA] = {
    .gfx_id = GFX_PIRANHA,
    .tiles_id = TILESET_PIRANHA,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_PIRANHA_BIG] = {
    .gfx_id = GFX_PIRANHA_BIG,
    .tiles_id = TILESET_PIRANHA_BIG,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_DART_FISH] = {
    .gfx_id = GFX_DART_FISH,
    .tiles_id = TILESET_DART_FISH,
    .init = actor_sprite_init,
    .drawer = render_sprite,
    .ticker = actor_sprite_tick,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_OCTOPUS] = {
    .gfx_id = GFX_OCTOPUS,
    .tiles_id = TILESET_OCTOPUS,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_FLY_DEMON] = {
    .gfx_id = GFX_FLY_DEMON,
    .tiles_id = TILESET_FLY_DEMON,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_MEERMAN] = {
    .gfx_id = GFX_MEERMAN,
    .tiles_id = TILESET_MEERMAN,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_GRAVITY,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY,
    .category_bits = CB_ENEMY,
    .category_mask = ~(CB_POWERUP|CB_ENEMY),
    .density = 0.5f,
  },
  [AT_MINE_SM] = {
    .gfx_id = GFX_MINE_SM,
    .tiles_id = TILESET_MINE_SM,
    .init = mine_init,
    .ticker = mine_tick,
    .collider = mine_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_ROTATES,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY - 1,
    .category_bits = CB_ENEMY,
    .category_mask = CB_PLAYER | CB_PROP,
    .density = 8.f,
  },
  [AT_MINE_MED] = {
    .gfx_id = GFX_MINE_MED,
    .tiles_id = TILESET_MINE_MED,
    .init = mine_init,
    .ticker = mine_tick,
    .collider = mine_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_ROTATES,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY - 1,
    .category_bits = CB_ENEMY,
    .category_mask = CB_PLAYER | CB_PROP,
    .density = 8.f,
  },
  [AT_MINE_BIG] = {
    .gfx_id = GFX_MINE_BIG,
    .tiles_id = TILESET_MINE_BIG,
    .init = mine_init,
    .ticker = mine_tick,
    .collider = mine_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_SOLID | AF_ROTATES,
    .collide_priority = CPRIORITY_ENEMY,
    .draw_priority = DPRIORITY_ENEMY - 1,
    .category_bits = CB_ENEMY,
    .category_mask = CB_PLAYER | CB_PROP,
    .density = 8.f,
  },
  [AT_CRYSTAL_BIG] = {
    .gfx_id = GFX_CRYSTAL_BIG,
    .tiles_id = TILESET_CRYSTAL_BIG,
    .init = crystal_init,
    .ticker = crystal_tick,
    .collider = crystal_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(crystal_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_POWERUP,
    .draw_priority = DPRIORITY_POWERUP,
    .category_bits = CB_POWERUP,
    .category_mask = CB_PLAYER,
  },
  [AT_CRYSTAL_SM] = {
    .gfx_id = GFX_CRYSTAL_SM,
    .tiles_id = TILESET_CRYSTAL_SM,
    .init = crystal_init,
    .ticker = crystal_tick,
    .collider = crystal_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(crystal_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_POWERUP,
    .draw_priority = DPRIORITY_POWERUP,
    .category_bits = CB_POWERUP,
    .category_mask = CB_PLAYER,
  },
  [AT_POWERUP] = {
    .gfx_id = GFX_POWERUP,
    .tiles_id = TILESET_POWERUP,
    .init = actor_sprite_init,
    .ticker = actor_sprite_tick,
    .collider = powerup_collide,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(actor_sprite_t),
    .flags = AF_STATIC,
    .collide_priority = CPRIORITY_POWERUP,
    .draw_priority = DPRIORITY_POWERUP,
    .category_bits = CB_POWERUP,
    .category_mask = CB_PLAYER,
  },
  [AT_CLIFF_PLATFORM_BIG] = {
    .gfx_id = GFX_CLIFF_PLATFORM_BIG,
    .tiles_id = TILESET_CLIFF_PLATFORM_BIG,
    .init = platform_init,
    .ticker = platform_tick,
    .set_target = platform_set_target,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(platform_t),
    .flags = AF_STATIC | AF_SOLID,
    .category_bits = CB_GROUND,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE,
  },
  [AT_CLIFF_PLATFORM_SM_1] = {
    .gfx_id = GFX_CLIFF_PLATFORM_SM_1,
    .tiles_id = TILESET_CLIFF_PLATFORM_SM_1,
    .init = platform_init,
    .ticker = platform_tick,
    .set_target = platform_set_target,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(platform_t),
    .flags = AF_STATIC | AF_SOLID,
    .category_bits = CB_GROUND,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE,
  },
  [AT_CLIFF_PLATFORM_SM_2] = {
    .gfx_id = GFX_CLIFF_PLATFORM_SM_2,
    .tiles_id = TILESET_CLIFF_PLATFORM_SM_2,
    .init = platform_init,
    .ticker = platform_tick,
    .set_target = platform_set_target,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(platform_t),
    .flags = AF_STATIC | AF_SOLID,
    .category_bits = CB_GROUND,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE,
  },
  [AT_CLIFF_PLATFORM_SM_3] = {
    .gfx_id = GFX_CLIFF_PLATFORM_SM_3,
    .tiles_id = TILESET_CLIFF_PLATFORM_SM_3,
    .init = platform_init,
    .ticker = platform_tick,
    .set_target = platform_set_target,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(platform_t),
    .flags = AF_STATIC | AF_SOLID,
    .category_bits = CB_GROUND,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE,
  },
  [AT_UNDERWATER_PLATFORM] = {
    .gfx_id = GFX_UNDERWATER_PLATFORM,
    .tiles_id = TILESET_UNDERWATER_PLATFORM,
    .init = platform_init,
    .ticker = platform_tick,
    .set_target = platform_set_target,
    .drawer = render_sprite,
    .cleanup = actor_sprite_cleanup,
    .struct_size = sizeof(platform_t),
    .flags = AF_STATIC | AF_SOLID,
    .category_bits = CB_GROUND,
    .category_mask = CB_PLAYER|CB_ENEMY|CB_PROP|CB_PROJECTILE,
  },
  [AT_SPACESHIP] = {
    .gfx_id = GFX_SPACESHIP,
    .tiles_id = TILESET_SPACESHIP,
    .init = spaceship_init,
    .ticker = spaceship_tick,
    .drawer = render_spaceship,
    .cleanup = spaceship_cleanup,
    .struct_size = sizeof(spaceship_t),
    .category_bits = CB_GROUND,
    .flags = AF_STATIC | AF_SOLID,
    .density = 20.f,
  },
  [AT_SUBMARINE] = {
    .gfx_id = GFX_SUB,
    .tiles_id = TILESET_SUB,
    .init = submarine_init,
    .ticker = submarine_tick,
    .drawer = render_submarine,
    .cleanup = submarine_cleanup,
    .struct_size = sizeof(submarine_t),
    .flags = AF_SOLID | AF_GRAVITY | AF_ROTATES,
    .collide_priority = CPRIORITY_PLAYER,
    .draw_priority = DPRIORITY_PLAYER,
    .category_bits = CB_PLAYER,
    .density = 8.f,
  },
};

const size_t actor_class_count = COUNT_OF(actor_classes);

