#pragma once

#include <libdragon.h>
#include <sys/queue.h>

#include "main.h"
#include "script.h"
#include "util.h"
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct map_s map_t;
typedef struct actor_s actor_t;

typedef struct controller_data pad_t;

#define TILE_SHIFT 4
#define TILE_PIXEL_DIM (1 << TILE_SHIFT)
#define TILE_FLIPX 0x8000
#define TILE_FLIPY 0x4000
#define TILE_FLIPD 0x2000
#define TILE_FLIP_MASK (TILE_FLIPX|TILE_FLIPY|TILE_FLIPD)
#define TILE_ID_MASK (~TILE_FLIP_MASK)

#define TID_MAP_SHIFT 4
#define MAX_TID 0x3fff

#define MAP_TILE_SIZE 16
#define CHUNK_SHIFT 4
#define CHUNK_PIXEL_SHIFT (CHUNK_SHIFT+TILE_SHIFT)
#define CHUNK_TILE_DIM (1 << CHUNK_SHIFT)
#define CHUNK_PIXEL_DIM (1 << CHUNK_PIXEL_SHIFT)
#define CHUNK_SIZE_SHIFT (CHUNK_SHIFT+CHUNK_SHIFT)

#define INVALID_SCRIPT 0xffffffff
#define INVALID_WAYPOINT 0xffffffff

#define ACTIVE_CLIP_EXTEND 128

#define DIALOG_FADE_LEN 20
#define DIALOG_MAX_LINES 3
#define DIALOG_COUNT_SHIFT 1

#define FADE_LEN 20
#define INV_FADE_LEN (1.f/(float) FADE_LEN)

#define CAMERA_ACCEL 1.0

typedef struct {
  uint16_t first_tid;
  uint16_t end_tid;
  uint8_t xmask;
  uint8_t yshift;
  union {
    sprite_t *image;
    uint32_t image_id;
  };
} tileset_header_t;

typedef struct {
  uintptr_t collision_offset;
  uint16_t anim_duration;
  uint16_t anim_next;
} frame_t;

typedef struct tiles_desc_s {
  uint32_t magic;
  int32_t offset_x;
  int32_t offset_y;
  uint16_t frame_width;
  uint16_t frame_height;
  uint16_t frame_count;
  uint16_t xmask;
  uint16_t xshift;
  uint16_t yshift;
  frame_t frames[];
} tiles_desc_t;

typedef struct {
  sprite_t *image;
  tiles_desc_t *tiles;
  uint32_t frame;
  float counter;
  float speed;
} sprite_anim_t;

typedef struct {
  float offset_x;
  float offset_y;
  float autoscroll_x;
  float autoscroll_y;
  float parallax_scale_x;
  float parallax_scale_y;
  color_t clear_top;
  color_t clear_bottom;
  uint8_t layer;
  bool repeat_x;
  bool repeat_y;
  sprite_anim_t anim;
} bg_header_t;

typedef struct actor_spawn_s {
  uint32_t type;
  int32_t x;
  int32_t y;
  uint32_t flags;
  uint16_t id;
  uint16_t rotation;
  union {
    void *arg;
    uintptr_t arg_int;
  };
} actor_spawn_t;

typedef struct {
  uint16_t gfx;
  uint16_t tiles;
  uint16_t flags;
  uint16_t initframe;
  int32_t offset_x;
  int32_t offset_y;
  uint16_t width_variance;
  uint16_t height_variance;
  uint16_t count;
  uint16_t count_variance;
  uint16_t speed;
  uint16_t speed_variance;
  uint16_t animspeed;
  uint16_t animspeed_variance;
  uint16_t angle;
  uint16_t angle_variance;
} particle_spawn_t;

typedef struct particle_s {
  LIST_ENTRY(particle_s) entry;
  sprite_anim_t anim;
  uint16_t flags;
  uint16_t initframe;
  float counter;
  float x;
  float y;
  float speed;
  float rot;
  float rotvel;
} particle_t;

typedef enum {
  PARTICLE_FLIPX      = 1<<0,
  PARTICLE_FLIPY      = 1<<1,
  PARTICLE_FLIPD      = 1<<2,
  PARTICLE_RANDOMFLIP = 1<<3,
  PARTICLE_LAYER_1    = 1<<6,
  PARTICLE_LOOPED     = 1<<7,
} particle_flags_t;

typedef struct waypoint_s {
  int32_t x;
  int32_t y;
  union {
    struct waypoint_s *next;
    uint32_t next_id;
  };
} waypoint_t;

typedef struct prop_s {
  uint32_t layer;
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t image_id;
  uint32_t tiles_id;
  uint32_t frame_drawn;
  uint32_t frame_ticked;
  sprite_anim_t anim;
  STAILQ_ENTRY(prop_s) active;
} prop_t;

typedef struct tile_chunk_s {
  int16_t x;
  int16_t y;
  int32_t px;
  int32_t py;
  uint8_t layers;
  uint8_t fg_layer;
  uint16_t prop_count;
  union {
    prop_t **props;
    uint32_t prop_offset;
  };
  body_t *body;
  //uint16_t[16] lightmask
  uint16_t tiles[];
} tile_chunk_t;

typedef struct {
  uint32_t magic;
  uint16_t tileset_count;
  uint16_t bg_count;
  uint16_t waypoint_count;
  uint16_t script_count;
  int16_t lower_x; // in chunks
  int16_t lower_y; // in chunks
  uint16_t width; // in chunks
  uint16_t height; // in chunks
  uint16_t chunk_count;
  uint16_t text_count;
  uint16_t actor_spawn_init_count;
  uint16_t actor_spawn_count;
  union {
    actor_spawn_t *actor_spawns;
    uint32_t actor_spawns_offset;
  };
  union {
    waypoint_t *waypoints;
    uint32_t waypoints_offset;
  };
  union {
    collision_t *collision;
    uint32_t collision_offset;
  };
  union {
    script_t **scripts;
    uint32_t scripts_offset;
  };
  union {
    const char **texts;
    uint32_t text_offset;
  };
  uint32_t music_id;
  uint32_t startup_script;
  int32_t parallax_origin_x;
  int32_t parallax_origin_y;
  int32_t camera_start_x;
  int32_t camera_start_y;
  int32_t water_line;
  color_t water_color;
  float gravity_x;
  float gravity_y;
} map_header_t;

typedef enum {
  FADE_NONE,
  FADE_OUT_COLOR,
  FADE_OUT_WIPE,
  FADE_IN_WIPE,
  FADE_IN_COLOR,
  FADE_INOUT_COLOR,
  FADE_INOUT_WIPE,
  FADE_CROSS,
  FADE_CROSS_WIPE,
} map_fade_t;

struct map_s {
  map_header_t *header;
  world_t *world;

  int16_t lower_x; // in chunks
  int16_t lower_y; // in chunks
  uint16_t width; // in chunks
  uint16_t height; // in chunks

  tileset_header_t *tilesets;
  bg_header_t *bgs;

  tile_chunk_t **chunks;
  LIST_HEAD(, actor_s) actors;
  LIST_HEAD(, actor_s) dead;
  actor_t *player;
  actor_t *hudplayer;

  uint32_t hud_crystal_counter;
  uint32_t death_counter;

  float camera_x;
  float camera_y;
  actor_t *camera_target;
  waypoint_t *camera_waypoint;
  float camera_vel;
  float camera_target_vel;

  float gravity_norm_x;
  float gravity_norm_y;

  float water_line;
  color_t water_color;
  float water_speed;
  float target_water_line;
  color_t target_water_color;

  uint32_t frame_counter;
  uint32_t render_counter;
  uint32_t state_flags;
  uint32_t cheats;

  pcg32_random_t rng;
  pcg32_random_t render_rng;

  STAILQ_HEAD(, prop_s) active_props;
  TAILQ_HEAD(, script_state_s) active_scripts;

  const char *dialog_text;
  size_t dialog_text_len;
  size_t dialog_text_lines;
  actor_t *dialog_target;
  int32_t dialog_counter;
  int32_t dialog_fade_counter;

  map_fade_t fade;
  color_t fade_color;
  uint16_t fade_counter;
  uint16_t next_map;

  uint32_t quake_counter;
  uint32_t quake_strength;

  uint32_t respawn_counter;

  uint32_t map_id;
  uint32_t pending_map;

  LIST_HEAD(, particle_s) particles;

  tileset_header_t *tid_map[(MAX_TID+1)>>TID_MAP_SHIFT];
};

typedef enum {
  MSF_PLAYER_CONTROL = 1 << 0,
  MSF_PLAYER_CHANGED = 1 << 1,
  MSF_CAMERA_MOVING  = 1 << 2,
  MSF_WATER_MOVING   = 1 << 3,
  MSF_RESPAWNING     = 1 << 4,
  MSF_FORCE_RESPAWN  = 1 << 5,
  MSF_ENDING         = 1 << 6,
} map_state_flags_t;

typedef enum {
  MC_DEBUG_DRAW = 1 << 0,
  MC_FREE_LOOK  = 1 << 4,
} map_cheats_t;

typedef void (*chunk_iter_t)(map_t *, const irect2_t *, tile_chunk_t *);

void map_load(const char *filename, map_t *map, uint32_t state_flags);
void map_unload(map_t *map);
global_state_t map_tick(map_t *map);
void map_transition(map_t *map, surface_t *screen);

void map_set_dialog(map_t *map, const char *text, actor_t *target);
void map_clear_dialog(map_t *map);

void map_call_delay(map_t *map, script_exec_t func, uint32_t frames);
void map_call_interval(map_t *map, script_exec_t func, uint32_t frames);

inline void *map_get_pointer(map_t *map, uintptr_t ptr) {
  if (ptr >= (uintptr_t) KSEG0_START_ADDR)
    return (void *) ptr;
  else
    return ((uint8_t *) map->header) + ptr;
}

tile_chunk_t *map_get_chunk(map_t *map, int32_t x, int32_t y);
void map_foreach_chunk_in_rect(map_t *map, const irect2_t *rect, chunk_iter_t func);
void map_foreach_chunk_in_rect_expand(map_t *map, const irect2_t *rect, int32_t expand, chunk_iter_t func);
void map_unload_props(map_t *map, bool all);

bool sprite_anim_tick(sprite_anim_t *anim);
void sprite_anim_cleanup(sprite_anim_t *anim);

void particle_spawn(map_t *map, float cx, float cy, const particle_spawn_t *spawn);
bool particle_in_rect(particle_t *particle, const irect2_t *rect);

#ifdef __cplusplus
}
#endif
