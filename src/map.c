#include <fmath.h>
#include <stdlib.h>
#include <inttypes.h>
#include <float.h>

#include "actors.h"
#include "cache.h"
#include "main.h"
#include "map.h"
#include "player.h"
#include "render.h"
#include "sound.h"
#include "util.h"

#define MAP_MAGIC 0x544d4150 // TMAP

#define NO_WATER ((int32_t) 0x80000000)
#define INVALID_FRAME ((uint16_t) 0xffff)

#define PROP_UNLOAD_FRAMES 60

static void map_tick_props(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void particle_destroy(particle_t *particle);

extern inline void *map_get_pointer(map_t *map, uintptr_t ptr);

// ********** MAP LOAD **********

void map_load(const char *filename, map_t *map, uint32_t state_flags) {
  if (map->header)
    map_unload(map);
  uint8_t *data = asset_load(filename, NULL);
  map_header_t *header = (void *) data;
  assertf(header->magic == MAP_MAGIC, "%s not a valid map", filename);

  map->header = header;
  map->rng = PCG32_INITIALIZER;
  map->rng.inc += get_ticks();
  map->rng.state -= get_ticks();
  map->render_rng = PCG32_INITIALIZER;
  map->rng.inc += get_ticks() * 2;
  map->rng.state -= get_ticks() * 2;
  map->lower_x = header->lower_x;
  map->lower_y = header->lower_y;
  map->width = header->width;
  map->height = header->height;
  map->tilesets = (tileset_header_t *) &header[1];
  map->bgs = (bg_header_t *) &map->tilesets[header->tileset_count];
  map->camera_x = header->camera_start_x;
  map->camera_y = header->camera_start_y;
  map->state_flags = state_flags;
  if (header->water_line == NO_WATER)
    map->water_line = map->target_water_line = FLT_MAX;
  else
    map->water_line = map->target_water_line = header->water_line;
  map->water_color = map->target_water_color = header->water_color;

  LIST_INIT(&map->actors);
  LIST_INIT(&map->dead);
  LIST_INIT(&map->particles);
  STAILQ_INIT(&map->active_props);
  TAILQ_INIT(&map->active_scripts);

  tile_chunk_t **chunks = (tile_chunk_t **) &map->bgs[header->bg_count];
  for (size_t i = 0; i < header->tileset_count; i++) {
    uint32_t id = map->tilesets[i].image_id;
    map->tilesets[i].image = sprite_pool_load(id);
    extern const char * const gfx_paths[];
    assertf(sprite_get_format(map->tilesets[i].image) == FMT_CI4,
        "tileset %s must be CI4 (got %s)", gfx_paths[id],
        tex_format_name(sprite_get_format(map->tilesets[i].image)));
  }
  for (size_t i = 0; i < header->bg_count; i++) {
    sprite_anim_t *bg = &map->bgs[i].anim;
    bg->image = sprite_pool_load((uintptr_t) bg->image);
    if (bg->tiles)
      bg->tiles = tileset_pool_load((uintptr_t) bg->tiles);
  }
  map->chunks = calloc(header->width * header->height, sizeof(tile_chunk_t *));
  assertf(map->chunks != NULL, "out of memory");
  for (size_t i = 0; i < header->chunk_count; i++) {
    tile_chunk_t *chunk = (tile_chunk_t *) &data[(uintptr_t) chunks[i]];
    chunk->props = (prop_t **) &data[chunk->prop_offset];
    for (size_t i = 0; i < chunk->prop_count; i++) {
      chunk->props[i] = (prop_t *) &data[(uintptr_t) chunk->props[i]];
    }
    size_t index = (chunk->y - map->lower_y) * header->width + chunk->x - map->lower_x;
    map->chunks[index] = chunk;
  }

  for (size_t i = 0; i < header->tileset_count; i++) {
    tileset_header_t *tileset = &map->tilesets[i];
    uint16_t end_tid = tileset->end_tid >> TID_MAP_SHIFT;
    for (uint16_t index = tileset->first_tid >> TID_MAP_SHIFT; index < end_tid; index++)
      map->tid_map[index] = tileset;
  }

  if (header->collision_offset)
    header->collision = (collision_t *) &data[header->collision_offset];

  map->world = world_new(map, header->gravity_x, header->gravity_y, map->water_line);

  if (header->text_count) {
    header->texts = (const char **) &data[header->text_offset];
    for (size_t i = 0; i < header->text_count; i++)
      header->texts[i] = (const char *) &data[(uintptr_t) header->texts[i]];
  }

  if (header->waypoint_count) {
    header->waypoints = (waypoint_t *) &data[header->waypoints_offset];
    for (size_t i = 0; i < header->waypoint_count; i++) {
      waypoint_t *waypoint = &header->waypoints[i];
      if (waypoint->next_id != INVALID_WAYPOINT)
        waypoint->next = &header->waypoints[waypoint->next_id];
      else
        waypoint->next = NULL;
    }
  }

  if (header->script_count) {
    header->scripts = (script_t **) &data[header->scripts_offset];
    for (size_t i = 0; i < header->script_count; i++)
      header->scripts[i] = (script_t *) &data[(uintptr_t) header->scripts[i]];
  }

  if (header->actor_spawn_count) {
    header->actor_spawns = (actor_spawn_t *) &data[header->actor_spawns_offset];
    for (size_t i = header->actor_spawn_count; i > 0; i--) {
      actor_spawn_t *spawn = &header->actor_spawns[i - 1];
      if (i - 1 < header->actor_spawn_init_count)
        actor_spawn(map, spawn);
    }
  }

  if (header->startup_script != INVALID_SCRIPT) {
    assertf(header->startup_script < header->script_count, "invalid startup script %"PRIu32, header->startup_script);
    script_start(map, header->startup_script, NULL);
  }

  /*
  if (header->music_id)
    sound_play_music(header->music_id, 0, 0);
    */
}

// ********** MAP TICK **********

global_state_t map_tick(map_t *map) {
  if (exception_reset_time() > 0)
    return ST_RESET;

  if (map->fade_counter) {
    map->fade_counter--;
    if (!map->fade_counter) {
      if (map->pending_map) {
        return ST_NEW_MAP;
      } else {
        if ((map->fade == FADE_CROSS || map->fade == FADE_CROSS_WIPE) && screen_grab.buffer)
          surface_free(&screen_grab);
        map->fade = FADE_NONE;
      }
    }
  }

  pad_t kdown = get_keys_down();

  if (map->respawn_counter) {
    map->respawn_counter--;
  }
  if (map->player
      && (((map->state_flags & MSF_RESPAWNING)
          && !map->respawn_counter
          && (kdown.c[0].A || kdown.c[0].B || kdown.c[0].start))
        || (map->state_flags & MSF_FORCE_RESPAWN))) {
    for (size_t i = map->header->actor_spawn_init_count; i > 0; i--) {
      actor_spawn_t *spawn = &map->header->actor_spawns[i - 1];
      if (spawn->flags & AF_CUR_PLAYER) {
        if (player_respawn(map, map->player, spawn)) {
          map->state_flags &= ~(MSF_RESPAWNING|MSF_FORCE_RESPAWN);
          break;
        }
      }
    }
  }

  bool player_control = !map->dialog_text_len
    && !map->fade_counter
    && (map->state_flags & MSF_PLAYER_CONTROL);

  if (player_control && kdown.c[0].start)
    return ST_PAUSE;

  map->frame_counter++;

  pad_t kpressed = get_keys_pressed();

  // tick bgs
  for (size_t i = 0; i < map->header->bg_count; i++) {
    bg_header_t *bg = &map->bgs[i];
    sprite_anim_tick(&bg->anim);
    if (bg->autoscroll_x) {
      bg->offset_x += INV_FPS * bg->autoscroll_x;
      if (bg->repeat_x) {
        float width = bg->anim.image->width;
        if (bg->offset_x >= width)
          bg->offset_x -= width;
        else if (bg->offset_x < 0)
          bg->offset_x += width;
      }
    }
    if (bg->autoscroll_y) {
      bg->offset_y += INV_FPS * bg->autoscroll_y;
      if (bg->repeat_y) {
        float height = bg->anim.image->height;
        if (bg->offset_y >= height)
          bg->offset_y -= height;
        else if (bg->offset_y < 0)
          bg->offset_y += height;
      }
    }
  }

  // tick screen effects
  if (map->quake_counter) {
    map->quake_counter--;
    if ((map->quake_counter & 4) && map->quake_strength)
      map->quake_strength--;
    if (map->quake_counter == 0)
      map->quake_strength = 0;
  }
  if (map->dialog_text_len) {
    if (map->dialog_counter < (map->dialog_text_len << DIALOG_COUNT_SHIFT)) {
      if (kpressed.c[0].L)
        map->dialog_counter = map->dialog_text_len << DIALOG_COUNT_SHIFT;

      if (map->dialog_fade_counter < DIALOG_FADE_LEN) {
        if (kpressed.c[0].A | kpressed.c[0].B)
          map->dialog_fade_counter += 2 << DIALOG_COUNT_SHIFT;
        else
          map->dialog_fade_counter++;
        map->dialog_fade_counter = MIN(map->dialog_fade_counter, DIALOG_FADE_LEN);
      } else {
        size_t inc;
        if (kpressed.c[0].A | kpressed.c[0].B) {
          inc = 4 << DIALOG_COUNT_SHIFT;
        } else {
          inc = 1;
        }
        for (; inc; inc--) {
          if ((map->dialog_counter & ((1<<DIALOG_COUNT_SHIFT)-1)) == 0) {
            size_t index = map->dialog_counter >> DIALOG_COUNT_SHIFT;
            char c = map->dialog_text[index];
            if (c == '\n') {
              if (map->dialog_text_lines >= DIALOG_MAX_LINES) {
                if (kpressed.c[0].A | kpressed.c[0].B) {
                  map->dialog_text = &map->dialog_text[index + 1];
                  map->dialog_text_len -= index + 1;
                  map->dialog_counter -= (index + 1) << DIALOG_COUNT_SHIFT;
                } else {
                  break;
                }
              } else {
                map->dialog_text_lines++;
              }
            }
          }
          map->dialog_counter++;
        }

        map->dialog_counter = MIN(map->dialog_counter, map->dialog_text_len << DIALOG_COUNT_SHIFT);
      }
    } else {
      if (map->dialog_fade_counter >= DIALOG_FADE_LEN) {
        if (kpressed.c[0].A | kpressed.c[0].B)
          map->dialog_fade_counter = DIALOG_FADE_LEN - 1;
      } else if (map->dialog_fade_counter > 0) {
        if (kpressed.c[0].A | kpressed.c[0].B)
          map->dialog_fade_counter -= 4;
        else
          map->dialog_fade_counter -= 1;

        map->dialog_fade_counter = MAX(map->dialog_fade_counter, 0);
      } else {
        map_clear_dialog(map);
      }
    }
  }
  if (map->water_line != map->target_water_line) {
    STEPTOWARDS(map->water_line, map->target_water_line, map->water_speed);
    world_move_water(map->world, map->water_line);
    if (map->water_line == map->target_water_line)
      map->state_flags &= ~MSF_WATER_MOVING;
  }
  if (memcmp(&map->target_water_color, &map->water_color, sizeof(color_t)) != 0) {
    STEP1(map->water_color.r, map->target_water_color.r);
    STEP1(map->water_color.g, map->target_water_color.g);
    STEP1(map->water_color.b, map->target_water_color.b);
    STEP1(map->water_color.a, map->target_water_color.a);
  }
  if (map->hud_crystal_counter)
    map->hud_crystal_counter--;

  // tick scripts
  {
    script_state_t *state, *next;
    TAILQ_FOREACH_SAFE(state, &map->active_scripts, entry, next) {
      script_tick(state, map);
    }
  }

  // tick camera and player controls
  if (map->cheats & MC_FREE_LOOK) {
    if (player_control) {
      if (abs(kpressed.c[0].x) > JOYSTICK_DEAD_ZONE)
        map->camera_x += (kpressed.c[0].x - SIGN((int)kpressed.c[0].x) * (float) JOYSTICK_DEAD_ZONE)
                         * (1.0 / JOYSTICK_DEAD_ZONE);
      if (abs(kpressed.c[0].y) > JOYSTICK_DEAD_ZONE)
        map->camera_y += (kpressed.c[0].y - SIGN((int)kpressed.c[0].y) * (float) JOYSTICK_DEAD_ZONE)
                         * (-1.0 / JOYSTICK_DEAD_ZONE);
    }
  } else {
    if (map->state_flags & MSF_CAMERA_MOVING) {
      float tx = FLT_MAX;
      float ty = FLT_MAX;
      if (map->camera_target) {
        world_get_actor_center(map->camera_target, &tx, &ty);
      } else if (map->camera_waypoint) {
        tx = map->camera_waypoint->x;
        ty = map->camera_waypoint->y;
      }
      if (tx != FLT_MAX && ty != FLT_MAX) {
        float dx = map->camera_x - tx;
        float dy = map->camera_y - ty;
        const float EPSILON = 0.5f;
        if (fabsf(dx) < EPSILON && fabsf(dy) < EPSILON) {
          map->camera_x = tx;
          map->camera_y = ty;
          if (map->camera_waypoint)
            map->camera_waypoint = map->camera_waypoint->next;
          else if (map->state_flags & MSF_PLAYER_CHANGED)
            map->state_flags &= ~(MSF_PLAYER_CHANGED|MSF_CAMERA_MOVING);
        } else {
          if (map->camera_vel && map->camera_target_vel
              && (!map->camera_waypoint || !map->camera_waypoint->next)) {
            float decel = CAMERA_ACCEL * 0.5 * map->camera_vel * (map->camera_vel + 1.0);
            if (dx * dx + dy + dy < decel * decel)
              map->camera_target_vel = 0.0;
          }
          STEPTOWARDS(map->camera_vel, map->camera_target_vel, CAMERA_ACCEL);
          float angle = atan2f(dy, dx);
          dx = cosf(angle) * map->camera_vel;
          STEPTOWARDS(map->camera_x, tx, dx);
          dy = sinf(angle) * map->camera_vel;
          STEPTOWARDS(map->camera_y, ty, dy);
        }
      }
    } else if (map->camera_target) {
      world_get_actor_center(map->camera_target, &map->camera_x, &map->camera_y);
    } else if (map->camera_waypoint) {
      map->camera_x = map->camera_waypoint->x;
      map->camera_y = map->camera_waypoint->y;
    }
    if (player_control && map->player) {
      player_movement(map, map->player, &kdown, &kpressed);
    }
  }

  // bound camera
  {
    int32_t bound;

    bound = (map->lower_x << CHUNK_PIXEL_SHIFT) + screen_half_width;
    if ((int32_t) map->camera_x < bound)
      map->camera_x = bound;
    bound = ((map->lower_x + map->width) << CHUNK_PIXEL_SHIFT) - screen_half_width;
    if ((int32_t) map->camera_x > bound)
      map->camera_x = bound;

    bound = (map->lower_y << CHUNK_PIXEL_SHIFT) + screen_half_height;
    if ((int32_t) map->camera_y < bound)
      map->camera_y = bound;
    bound = ((map->lower_y + map->height) << CHUNK_PIXEL_SHIFT) - screen_half_height;
    if ((int32_t) map->camera_y > bound)
      map->camera_y = bound;
  }

  sound_set_listener_pos(map->camera_x, map->camera_y);

  // tick physics
  world_tick(map->world);

  // tick actors
  {
    actor_t *actor;
    LIST_FOREACH(actor, &map->actors, map) {
      if (actor->ticker)
        actor->ticker(map, actor);
    }
  }

  // tick sound movement
  {
    actor_t *actor;
    // TODO make a separate list for actors playing sounds
    LIST_FOREACH(actor, &map->actors, map) {
      if (actor->sound_channel != -1) {
        float x, y;
        world_get_actor_center(actor, &x, &y);
        sound_update_position(actor->sound_channel, x, y);
      }
    }
  }

  // remove destroyed actors
  {
    actor_t *actor, *next;
    LIST_FOREACH_SAFE(actor, &map->dead, map, next) {
      uint32_t flags = actor->flags;
      if (flags & AF_DESTROYED) {
        actor_finalize(actor);
      } else if (flags & AF_DESTROYING) {
        flags |= AF_DESTROYED;
        if (actor->body) {
          world_body_destroy(actor->body);
          actor->body = NULL;
        }
      }
    }
  }

  // tick particles
  {
    const irect2_t active_rect = {
      .x0 = map->camera_x - screen_half_width - ACTIVE_CLIP_EXTEND,
      .y0 = map->camera_y - screen_half_height - ACTIVE_CLIP_EXTEND,
      .x1 = map->camera_x + screen_half_width + ACTIVE_CLIP_EXTEND,
      .y1 = map->camera_y + screen_half_height + ACTIVE_CLIP_EXTEND,
    };

    particle_t *particle, *next;
    LIST_FOREACH_SAFE(particle, &map->particles, entry, next) {
      bool ticked = sprite_anim_tick(&particle->anim);
      if (ticked && particle->initframe != INVALID_FRAME && particle->initframe == particle->anim.frame) {
        particle_destroy(particle);
        continue;
      }
      if (particle->rotvel) {
        particle->rot += particle->rotvel;
        if (particle->rot >= M_PI * 2.0)
          particle->rot -= M_PI * 2.0;
        else if (particle->rot <= M_PI * 2.0)
          particle->rot += M_PI * 2.0;
      }
      if (particle->speed) {
        particle->x += cosf(particle->rot) * particle->speed;
        particle->y += sinf(particle->rot) * particle->speed;
      }
      if (!particle_in_rect(particle, &active_rect)) {
        particle_destroy(particle);
        continue;
      }
    }
    map_foreach_chunk_in_rect(map, &active_rect, map_tick_props);
  }

  return ST_GAME;
}

static void map_tick_props(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  prop_t **props = chunk->props;
  size_t prop_count = chunk->prop_count;
  for (size_t i = 0; i < prop_count; i++) {
    prop_t *prop = props[i];
    if (prop->anim.tiles && prop->frame_ticked != map->frame_counter) {
      sprite_anim_tick(&prop->anim);
      prop->frame_ticked = map->frame_counter;
    }
  }
}

void map_transition(map_t *map, surface_t *screen) {
  if (!map->pending_map || map->fade_counter)
    return;
  extern const char * const maps_paths[];
  map_fade_t fade = map->fade;
  color_t fade_color = map->fade_color;
  playersave_t save;

  if (fade == FADE_CROSS || fade == FADE_CROSS_WIPE) {
    grab_screen(screen);
    if (!screen_grab.buffer)
      fade = FADE_NONE;
  }

  if (map->player)
    player_save(map->player, &save);

  map_load(maps_paths[map->pending_map], map, map->state_flags);
  map->map_id = map->pending_map;

  if (map->player)
    player_restore(map->player, &save);

  if (fade == FADE_IN_WIPE || fade == FADE_INOUT_WIPE) {
    map->fade = FADE_IN_WIPE;
    map->fade_counter = FADE_LEN;
  } else if (fade == FADE_IN_COLOR || fade == FADE_INOUT_COLOR) {
    map->fade = FADE_IN_COLOR;
    map->fade_counter = FADE_LEN;
    map->fade_color = fade_color;
  } else if (fade == FADE_CROSS || fade == FADE_CROSS_WIPE) {
    map->fade = fade;
    map->fade_counter = FADE_LEN;
  }
}

void map_set_dialog(map_t *map, const char *text, actor_t *target) {
  map->dialog_text = text;
  map->dialog_text_len = strlen(text);
  map->dialog_text_lines = 1;
  map->dialog_counter = 0;
  map->dialog_fade_counter = 0;
  map->dialog_target = target;
}

void map_clear_dialog(map_t *map) {
  map->dialog_text = NULL;
  map->dialog_text_len = 0;
  map->dialog_text_lines = 0;
  map->dialog_counter = 0;
  map->dialog_fade_counter = 0;
  map->dialog_target = NULL;
}

void map_call_delay(map_t *map, script_exec_t func, uint32_t frames) {
  script_builder_t builder;
  script_builder_init(&builder);
  if (frames)
    script_builder_push_delay(&builder, frames);
  script_builder_push_exec(&builder, func);
  dynscript_t *script = script_builder_finish_exit(&builder, map);
  script_start_dynamic(map, script, NULL);
  script_unref(script);
}

void map_call_interval(map_t *map, script_exec_t func, uint32_t frames) {
  script_builder_t builder;
  script_builder_init(&builder);
  script_t *loop = script_builder_push_delay(&builder, frames);
  script_builder_push_exec(&builder, func);
  dynscript_t *script = script_builder_finish_jump_dynamic(&builder, map, loop);
  script_start_dynamic(map, script, NULL);
  script_unref(script);
}

tile_chunk_t *map_get_chunk(map_t *map, int32_t x, int32_t y) {
  x = (x >> CHUNK_PIXEL_SHIFT) - map->lower_x;
  if (x < 0 || x >= map->width)
    return NULL;
  y = (y >> CHUNK_PIXEL_SHIFT) - map->lower_y;
  if (y < 0 || y >= map->height)
    return NULL;
  return map->chunks[y * map->width + x];
}

void map_foreach_chunk_in_rect(map_t *map, const irect2_t *rect, chunk_iter_t func) {
  for (int32_t y = rect->y0; ; y += CHUNK_PIXEL_DIM) {
    for (int32_t x = rect->x0; ; x += CHUNK_PIXEL_DIM) {
      tile_chunk_t *chunk = map_get_chunk(map, x, y);
      if (chunk)
        func(map, rect, chunk);
      if (x > rect->x1)
        break;
    }
    if (y > rect->y1)
      break;
  }
}

void map_foreach_chunk_in_rect_expand(map_t *map, const irect2_t *rect, int32_t expand, chunk_iter_t func) {
  int32_t x1 = rect->x1 + expand;
  int32_t y1 = rect->y1 + expand;
  for (int32_t y = rect->y0 - expand; ; y += CHUNK_PIXEL_DIM) {
    for (int32_t x = rect->x0 - expand; ; x += CHUNK_PIXEL_DIM) {
      tile_chunk_t *chunk = map_get_chunk(map, x, y);
      if (chunk)
        func(map, rect, chunk);
      if (x > x1)
        break;
    }
    if (y > y1)
      break;
  }
}

void map_unload_props(map_t *map, bool all) {
  prop_t *prop, *next, *prev = NULL;
  STAILQ_FOREACH_SAFE(prop, &map->active_props, active, next) {
    if (all || map->frame_counter - PROP_UNLOAD_FRAMES > prop->frame_drawn) {
      if (prop->anim.tiles) {
        tileset_pool_unload(prop->anim.tiles);
        prop->anim.tiles = NULL;
      }
      if (prop->anim.image) {
        sprite_pool_unload(prop->anim.image);
        prop->anim.image = NULL;
      }
      if (prev)
        prev->active.stqe_next = prop->active.stqe_next;
      else
        map->active_props.stqh_first = prop->active.stqe_next;
      if (STAILQ_LAST(&map->active_props, prop_s, active) == prop) {
        if (prev)
          map->active_props.stqh_last = &prev->active.stqe_next;
        else
          map->active_props.stqh_last = &map->active_props.stqh_first;
      }
      prop->active.stqe_next = NULL;
    } else {
      prev = prop;
    }
  }
}

// ********** MAP UNLOAD **********

void map_unload(map_t *map) {
  {
    particle_t *particle, *next;
    LIST_FOREACH_SAFE(particle, &map->particles, entry, next) {
      LIST_REMOVE(particle, entry);
      free(particle);
    }
  }
  {
    script_state_t *state, *next;
    TAILQ_FOREACH_SAFE(state, &map->active_scripts, entry, next)
      script_destroy(state, map);
  }
  {
    actor_t *actor, *next;
    LIST_FOREACH_SAFE(actor, &map->actors, map, next)
      actor_finalize(actor);
    LIST_FOREACH_SAFE(actor, &map->dead, map, next)
      actor_finalize(actor);
  }
  map_unload_props(map, true);
  for (size_t i = 0; i < map->header->tileset_count; i++)
    sprite_pool_unload(map->tilesets[i].image);
  for (size_t i = 0; i < map->header->bg_count; i++) {
    sprite_pool_unload(map->bgs[i].anim.image);
    if (map->bgs[i].anim.tiles)
      tileset_pool_unload(map->bgs[i].anim.tiles);
  }
  if (map->chunks)
    free(map->chunks);
  world_destroy(map->world);
  free(map->header);
  memset(map, 0, sizeof(map_t));
}

bool sprite_anim_tick(sprite_anim_t *anim) {
  tiles_desc_t *tiles = anim->tiles;
  if (tiles) {
    frame_t *frame = &tiles->frames[anim->frame];
    if (frame->anim_duration) {
      if (((uint32_t) anim->counter) >= (uint32_t) frame->anim_duration) {
        anim->frame = frame->anim_next;
        anim->counter -= (float) frame->anim_duration;
        return true;
      } else {
        anim->counter += anim->speed;
      }
    }
  }
  return false;
}

void sprite_anim_cleanup(sprite_anim_t *anim) {
  if (anim->image)
    sprite_pool_unload(anim->image);
  if (anim->tiles)
    tileset_pool_unload(anim->tiles);
}

void particle_spawn(map_t *map, float cx, float cy, const particle_spawn_t *spawn) {
  uint32_t n = spawn->count;
  if (spawn->count_variance) {
    n += RANDN(&map->rng, spawn->count_variance << 1);
    if (n > spawn->count_variance)
      n -= spawn->count_variance;
    else
      n = 1;
  }
  if (n == 0)
    n = 1;
  cx += spawn->offset_x;
  cy += spawn->offset_y;
  while (n--) {
    particle_t *particle = calloc(1, sizeof(particle_t));
    LIST_INSERT_HEAD(&map->particles, particle, entry);
    particle->anim.image = sprite_pool_load(spawn->gfx);
    particle->anim.tiles = tileset_pool_load(spawn->tiles);
    particle->anim.frame = particle->initframe = spawn->initframe;
    particle->flags = spawn->flags;
    particle->x = cx;
    particle->y = cy;
    particle->rot = ang16_to_radians(spawn->angle);
    particle->speed = ((float) spawn->speed) * (1.0/256.0);
    particle->anim.speed = spawn->animspeed ? ((float) spawn->animspeed) * (1.0/256.0) : 1.f;

    if (spawn->width_variance)
      particle->x += RANDN(&map->rng, spawn->width_variance) - (float) (spawn->width_variance >> 1);
    if (spawn->height_variance)
      particle->y += RANDN(&map->rng, spawn->height_variance) - (float) (spawn->height_variance >> 1);
    if (spawn->speed_variance)
      particle->speed += RANDN(&map->rng, spawn->speed_variance) - (float) (spawn->speed_variance >> 1);
    if (spawn->animspeed_variance)
      particle->anim.speed += RANDN(&map->rng, spawn->animspeed_variance) - (float) (spawn->animspeed_variance >> 1);
    if (spawn->angle_variance) {
      float r = ang16_to_radians(RANDN(&map->rng, spawn->angle_variance));
      particle->rot += r - r * 0.5;
    }
    if (particle->flags & PARTICLE_RANDOMFLIP)
      particle->flags ^= RANDN(&map->rng, 8);
  }
}

bool particle_in_rect(particle_t *particle, const irect2_t *rect) {
  tiles_desc_t *tiles = particle->anim.tiles;

  int32_t width = particle->anim.image->width;
  int32_t height = tiles->frame_height;
  int32_t offset_x = tiles->offset_x;
  int32_t offset_y = tiles->offset_y;

  if (particle->flags & PARTICLE_FLIPX)
    offset_x = -offset_x;
  if (particle->flags & PARTICLE_FLIPY)
    offset_y = -offset_y;
  if (particle->flags & PARTICLE_FLIPD) {
    SWAP(width, height);
    SWAP(offset_x, offset_y);
  }

  if (particle->rot)
    width *= M_SQRT2;

  int32_t x0 = particle->x - (width >> 1) + offset_x;
  if (x0 >= rect->x1)
    return false;
  if (x0 + width < rect->x0)
    return false;

  if (particle->rot)
    height *= M_SQRT2;

  int32_t y0 = particle->y - (height >> 1) + offset_y;
  if (y0 >= rect->y1)
    return false;
  if (y0 + height < rect->y0)
    return false;

  return true;
}

static void particle_destroy(particle_t *particle) {
  sprite_anim_cleanup(&particle->anim);
  LIST_REMOVE(particle, entry);
  free(particle);
}
