#include <fmath.h>
#include <float.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <libdragon.h>
#include <stdlib.h>
#include <stdarg.h>

#include "render.h"
#include "actors.h"
#include "cache.h"
#include "main.h"
#include "map.h"
#include "util.h"
#include "assets.h"
#include "player.h"

#define DEPTH_BOUND 8192.0
#define Z_SCALE_FACTOR (1.0/64.0)

#define MAX_VIS_ACTORS 512

#define DIALOG_WIDTH 200
#define DIALOG_PADDING 4
#define DIALOG_HEIGHT (DIALOG_MAX_LINES * 12 + (DIALOG_PADDING << 1))

#define FONT_COLOR (RGBA32(232, 250, 190, 255))

typedef struct {
  actor_t *actors[MAX_VIS_ACTORS];
  size_t count;
  int32_t frame_id;
} actor_visarray_t;

static bool frame_used_gl;
static sprite_t *water_side[3];
static sprite_t *water_top[3];

static void render_bgs(map_t *map, const irect2_t *rect, uint8_t layer);
static void render_prop_layer_0(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_prop_layer_1(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_prop_layer_2(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_prop_layer_3(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_bg_tiles(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_fg_tiles(map_t *map, const irect2_t *rect, tile_chunk_t *chunk);
static void render_water_plane(map_t *map, const irect2_t *rect, uint8_t layer);
static void render_particles(map_t *map, const irect2_t *rect, uint8_t layer);
static bool render_queue_actor(actor_t *actor, const irect2_t *rect, void *arg);

static int actor_vis_sort(const void *a, const void *b);

static void setup_scene_gl(const irect2_t *rect);

void render_scene(map_t *map) {
  frame_used_gl = false;

  rdpq_clear_z(0xfffc);

  irect2_t rect = {
    .x0 = map->camera_x - screen_half_width,
    .y0 = map->camera_y - screen_half_height,
    .x1 = map->camera_x + screen_half_width,
    .y1 = map->camera_y + screen_half_height,
  };
  if (map->quake_counter) {
    int32_t qx = RANDN(&map->rng, map->quake_strength << 1) - (int32_t) map->quake_strength;
    int32_t qy = RANDN(&map->rng, map->quake_strength << 1) - (int32_t) map->quake_strength;
    rect.x0 += qx;
    rect.x1 += qx;
    rect.y0 += qy;
    rect.y1 += qy;
  }

  rdpq_set_mode_copy(true);
  render_bgs(map, &rect, 0);
  render_water_plane(map, &rect, 0);
  rdpq_set_mode_copy(true);
  map_foreach_chunk_in_rect(map, &rect, render_prop_layer_0);
  map_foreach_chunk_in_rect(map, &rect, render_bg_tiles);
  map_foreach_chunk_in_rect(map, &rect, render_prop_layer_1);
  render_bgs(map, &rect, 1);

  render_particles(map, &rect, 0);
  {
    actor_visarray_t vis;
    vis.count = 0;
    vis.frame_id = map->render_counter;
    world_foreach_actor_in_rect(map->world, &rect, ACTIVE_CLIP_EXTEND, render_queue_actor, &vis);
    qsort(vis.actors, vis.count, sizeof(actor_t *), actor_vis_sort);
    for (size_t i = 0; i < vis.count; i++) {
      actor_t *actor = vis.actors[i];
      actor->drawer(actor, &rect);
    }
  }
  render_particles(map, &rect, 1);

  rdpq_set_mode_copy(true);
  render_bgs(map, &rect, 2);
  map_foreach_chunk_in_rect(map, &rect, render_prop_layer_2);
  map_foreach_chunk_in_rect(map, &rect, render_fg_tiles);
  map_foreach_chunk_in_rect(map, &rect, render_prop_layer_3);

  render_water_plane(map, &rect, 1);
  render_bgs(map, &rect, 3);

  map_unload_props(map, false);

  if (map->dialog_text_len) {
    uint32_t dx0, dy0, dx1, dy1;
    if (map->dialog_target) {
      world_get_actor_position(map->dialog_target, &dx0, &dy0);
      dx0 = MAX(dx0, rect.x0 + 24);
      dy0 = MAX(dy0, rect.y0 + 24);
      dx0 = MIN(dx0, rect.x1 - 24 - DIALOG_WIDTH);
      dy0 = MIN(dy0, rect.y1 - 24 - DIALOG_WIDTH);
    } else {
      dx0 = screen_half_width - (DIALOG_WIDTH >> 1);
      dy0 = screen_half_width + 32 + DIALOG_HEIGHT;
    }
    if (map->dialog_fade_counter < DIALOG_FADE_LEN) {
      float fac = map->dialog_fade_counter * (1.0/(float)DIALOG_FADE_LEN);
      int32_t w = DIALOG_WIDTH * fac;
      int32_t h = DIALOG_HEIGHT * fac;
      dx0 += (DIALOG_WIDTH - w) >> 1;
      dy0 += (DIALOG_HEIGHT - h) >> 1;
      dx1 = dx0 + w;
      dy1 = dy0 + h;
    } else {
      dx1 = dx0 + DIALOG_WIDTH;
      dy1 = dy0 + DIALOG_HEIGHT;
    }
    rdpq_set_mode_fill(FONT_COLOR);
    rdpq_fill_rectangle(dx0 - 2, dy0 - 1, dx0,     dy1 + 1);
    rdpq_fill_rectangle(dx1,     dy0 - 1, dx1 + 2, dy1 + 1);
    rdpq_fill_rectangle(dx0 - 1, dy0 - 2, dx1 + 1, dy0    );
    rdpq_fill_rectangle(dx0 - 1, dy1    , dx1 + 1, dy1 + 2);
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 128));
    rdpq_mode_end();
    rdpq_fill_rectangle(dx0, dy1, dx1, dy1);
    if (map->dialog_fade_counter >= DIALOG_FADE_LEN) {
      rdpq_textparms_t parms = {
        .width = DIALOG_WIDTH - (DIALOG_PADDING << 1), .height = DIALOG_WIDTH - (DIALOG_PADDING << 1)
      };
      rdpq_text_printn(&parms, FONT_MED, dx0 + DIALOG_PADDING, dy0 + DIALOG_PADDING,
                       map->dialog_text, map->dialog_counter >> DIALOG_COUNT_SHIFT);
    }
  }

#ifndef NDEBUG
  if (map->cheats & MC_DEBUG_DRAW)
    world_debug_draw(map->world);
#endif

  if (map->hudplayer && map->hudplayer->type == AT_YELLOW && (map->state_flags & MSF_PLAYER_CONTROL)) {
    player_t *player = (player_t *) map->hudplayer;
    render_shadow_printf(NULL, FONT_SMALL, 28, 24, "ENERGY    %d", MAX(0, player->mob.health));
    //if (map->hud_crystal_counter)
    render_shadow_printf(NULL, FONT_SMALL, 16, 34, "CRYSTALS    %d", MAX(0, (int)player->crystals));
  }

  if (map->state_flags & MSF_ENDING) {
    rdpq_textparms_t parms = {
      .width = display_get_width(),
      .height = display_get_height(),
      .align = ALIGN_CENTER,
      .valign = VALIGN_CENTER,
    };
    render_shadow_printf(
        &parms, FONT_MED, 0.f, 16.f,
        "FINAL SCORE\n%d CRYSTALS", player_highscore.crystals);
  }

  map->render_counter++;
}

static bool render_queue_actor(actor_t *actor, const irect2_t *rect, void *arg) {
  if (actor->drawer) {
    actor_visarray_t *array = arg;
    if (actor->frame_drawn != array->frame_id) {
      array->actors[array->count++] = actor;
      actor->frame_drawn = array->frame_id;
      if (array->count >= MAX_VIS_ACTORS)
        return false;
    }
  }
  return true;
}

static int actor_vis_sort(const void *va, const void *vb) {
  actor_t *a = *(actor_t **)va;
  actor_t *b = *(actor_t **)vb;
  int16_t pa = a->cls->draw_priority;
  int16_t pb = b->cls->draw_priority;
  if (pa < pb)
    return -1;
  else if (pa > pb)
    return 1;
  else if (a < b)
    return -1;
  else if (a > b)
    return 1;
  return 0;
}

static void render_bg(const map_t *map, const irect2_t *rect, bg_header_t *bg) {
  sprite_t *image = bg->anim.image;
  int32_t dwidth;
  int32_t dheight;

  int32_t x0 = -rect->x0;
  int32_t y0 = -rect->y0;

  if (F2I(bg->parallax_scale_x) != F2I(F1))
    x0 = (x0 - map->header->parallax_origin_x) * bg->parallax_scale_x;
  if (F2I(bg->parallax_scale_y) != F2I(F1))
    y0 = (y0 - map->header->parallax_origin_y) * bg->parallax_scale_y;
  x0 += (int32_t) bg->offset_x;
  y0 += (int32_t) bg->offset_y;

  dwidth = display_get_width();
  x0 -= (428 - dwidth) >> 1;

  int32_t x1;
  int32_t y1;

  rdpq_blitparms_t parms = {
    .width = image->width,
    .height = image->height,
  };
  tiles_desc_t *tiles = bg->anim.tiles;
  if (tiles) {
    parms.height = bg->anim.tiles->frame_height;
    if (tiles->xmask) {
      uint32_t tid = bg->anim.frame;
      parms.s0 = (tid & tiles->xmask) << tiles->xshift;
      parms.width = tiles->frame_width;
      parms.t0 = (tid & ~tiles->xmask) << tiles->yshift;
    } else {
      parms.t0 = parms.height * bg->anim.frame;
    }
  }

  dheight = display_get_height();
  y1 = y0 + parms.height;
  if (!bg->repeat_y) {
    if (y1 < 0 && bg->clear_bottom.a == 0)
      return;
    if (((int) y0) >= dheight && bg->clear_top.a == 0)
      return;
  }
  x1 = x0 + parms.width;
  if (!bg->repeat_x) {
    if (x1 < 0)
      return;
    if (((int) x0) >= dwidth)
      return;
  } else {
    x0 %= parms.width;
    if (x0 > 0)
      x0 -= parms.width;
    x1 = dwidth;
  }

  if (bg->repeat_y) {
    y0 %= parms.height;
    if (y0 > 0)
      y0 -= parms.height;
    y1 = dheight;
  } else {
    if (bg->clear_top.a && y0 > 0) {
      rdpq_set_mode_fill(bg->clear_top);
      rdpq_fill_rectangle(0, 0, dwidth, y0);
    }
    if (bg->clear_bottom.a && y1 < dheight) {
      rdpq_set_mode_fill(bg->clear_bottom);
      rdpq_fill_rectangle(0, y1, dwidth, dheight);
    }
  }

  rdpq_set_mode_copy(true);
  for (; y0 < y1; y0 += parms.height)
    for (; x0 < x1; x0 += parms.width)
      rdpq_sprite_blit(image, x0, y0, &parms);
}

static void render_bgs(map_t *map, const irect2_t *rect, uint8_t layer) {
  for (size_t i = 0; i < map->header->bg_count; i++) {
    bg_header_t *bg = &map->bgs[i];
    if (bg->layer == layer)
      render_bg(map, rect, bg);
  }
}

static bool prop_in_rect(prop_t *prop, const irect2_t *rect) {
  int32_t x0 = prop->x;
  if (x0 >= rect->x1)
    return false;
  if (x0 + prop->width < rect->x0)
    return false;
  int32_t y0 = prop->y;
  if (y0 >= rect->y1)
    return false;
  if (y0 + prop->height < rect->y0)
    return false;
  return true;
}

static void render_props(map_t *map, const irect2_t *rect, tile_chunk_t *chunk, uint8_t layer) {
  prop_t **props = chunk->props;
  size_t prop_count = chunk->prop_count;
  for (size_t i = 0; i < prop_count; i++) {
    prop_t *prop = props[i];
    if (prop->layer == layer && prop->frame_drawn != map->render_counter && prop_in_rect(prop, rect)) {
      if (!prop->anim.image)
        prop->anim.image = sprite_pool_load(prop->image_id);
      if (!prop->anim.tiles && prop->tiles_id)
        prop->anim.tiles = tileset_pool_load(prop->tiles_id);
      rdpq_blitparms_t parms = {};
      tiles_desc_t *tiles = prop->anim.tiles;
      if (tiles) {
        parms.height = prop->anim.tiles->frame_height;
        if (tiles->xmask) {
          uint32_t tid = prop->anim.frame;
          parms.s0 = (tid & tiles->xmask) << tiles->xshift;
          parms.width = tiles->frame_width;
          parms.t0 = (tid & ~tiles->xmask) << tiles->yshift;
        } else {
          parms.t0 = parms.height * prop->anim.frame;
        }
      }
      rdpq_sprite_blit(prop->anim.image, prop->x - rect->x0, prop->y - rect->y0, &parms);
      prop->frame_drawn = map->render_counter;
    }
    prop++;
  }
}

static void render_particle(particle_t *particle, const irect2_t *rect) {
  bool standard = F2I(particle->rot) != 0;

  sprite_t *image = particle->anim.image;
  tiles_desc_t *tiles = particle->anim.tiles;
  int32_t offset_x = tiles->offset_x;
  int32_t offset_y = tiles->offset_y;
  uint32_t height = tiles->frame_height;
  rdpq_blitparms_t parms = {
    .t0 = height * particle->anim.frame,
    .height = height,
    .theta = particle->rot,
    .cx = image->width >> 1,
    .cy = height >> 1,
  };
  if (particle->flags & PARTICLE_FLIPX) {
    parms.flip_x = true;
    offset_x = -offset_x;
    standard = true;
  }
  if (particle->flags & PARTICLE_FLIPY) {
    parms.flip_y = true;
    offset_y = -offset_y;
    standard = true;
  }
  if (particle->flags & PARTICLE_FLIPD) {
    parms.flip_x = !parms.flip_x;
    parms.theta += M_PI*0.5;
    standard = true;
  }

  rdpq_mode_begin();
  if (standard) {
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
  } else {
    rdpq_set_mode_copy(true);
  }
  rdpq_mode_zoverride(true, 0.5, 0);
  rdpq_mode_end();
  rdpq_sprite_blit(image,
      (int32_t)particle->x - rect->x0 - parms.cx + offset_x,
      (int32_t)particle->y - rect->y0 - parms.cy + offset_y,
      &parms);
}

static void render_particles(map_t *map, const irect2_t *rect, uint8_t layer) {
  particle_t *particle;
  LIST_FOREACH(particle, &map->particles, entry) {
    if ((layer == 1) == !!(particle->flags & PARTICLE_LAYER_1))
      render_particle(particle, rect);
  }
}

static void render_chunk_tiles(map_t *map, const irect2_t *rect, tile_chunk_t *chunk, bool fg) {
  uint8_t layer_start, layer_end;

  rdpq_set_mode_copy(true);
  rdpq_mode_tlut(TLUT_RGBA16);

  if (fg) {
    layer_start = chunk->fg_layer;
    layer_end = chunk->layers;
  } else {
    layer_start = 0;
    layer_end = chunk->fg_layer;
  }

  int32_t xstart = (MAX(rect->x0, chunk->px) - chunk->px) >> CHUNK_SHIFT;
  int32_t xend = (MIN(ALIGN(rect->x1, CHUNK_TILE_DIM), chunk->px + CHUNK_PIXEL_DIM) - chunk->px) >> CHUNK_SHIFT;
  int32_t ystart = (MAX(rect->y0, chunk->py) - chunk->py) >> CHUNK_SHIFT << CHUNK_SHIFT;
  int32_t yend = (MIN(ALIGN(rect->y1, CHUNK_TILE_DIM), chunk->py + CHUNK_PIXEL_DIM) - chunk->py) >> CHUNK_SHIFT << CHUNK_SHIFT;

  tileset_header_t *last_tileset = NULL;
  uint16_t last_tid = 0;
  uint32_t s;
  uint32_t t;
  bool copy = true;

  for (uint8_t layer = layer_start; layer < layer_end; layer++) {
    uint16_t *tiles = &chunk->tiles[layer << CHUNK_SIZE_SHIFT];
    for (int32_t y = ystart; y < yend; y += CHUNK_TILE_DIM) {
      for (int32_t x = xstart; x < xend; x++) {
        uint16_t tile = tiles[y + x];
        if (tile == 0)
          continue;
        uint16_t tid = tile & TILE_ID_MASK;
        if (tid != last_tid) {
          last_tid = tid;
          tileset_header_t *tileset = map->tid_map[tid >> TID_MAP_SHIFT];
          tid -= tileset->first_tid;
          sprite_t *sprite = tileset->image;
          if (tileset != last_tileset) {
            last_tileset = tileset;
            rdpq_tex_upload_tlut(sprite_get_palette(sprite), 0, 16);
          }
          s = (tid & tileset->xmask) << TILE_SHIFT;
          t = (tid >> tileset->yshift) << TILE_SHIFT;
          surface_t surf = sprite_get_pixels(sprite);
          rdpq_tex_upload_sub(TILE0, &surf, NULL, s, t, s + TILE_PIXEL_DIM, t + TILE_PIXEL_DIM);
        }
        int32_t x0 = (x << CHUNK_SHIFT) + chunk->px - rect->x0;
        int32_t y0 = y + chunk->py - rect->y0;
        int32_t x1 = x0 + TILE_PIXEL_DIM;
        int32_t y1 = y0 + TILE_PIXEL_DIM;
        if (tile & TILE_FLIP_MASK) {
          if (copy) {
            copy = false;
            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_tlut(TLUT_RGBA16);
          }
        } else if (!copy) {
          copy = true;
          rdpq_set_mode_copy(true);
          rdpq_mode_tlut(TLUT_RGBA16);
        }
        if (tile & TILE_FLIPX) {
          SWAP(x0, x1);
          x0 -= 1;
          x1 -= 1;
        }
        if (tile & TILE_FLIPY) {
          SWAP(y0, y1);
          y0 -= 1;
          y1 -= 1;
        }
        if (tile & TILE_FLIPD)
          rdpq_texture_rectangle_flip(TILE0, x0, y0, x1, y1, s, t);
        else
          rdpq_texture_rectangle(TILE0, x0, y0, x1, y1, s, t);
      }
    }
  }
}

static void render_prop_layer_0(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_props(map, rect, chunk, 0);
}

static void render_prop_layer_1(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_props(map, rect, chunk, 1);
}

static void render_prop_layer_2(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_props(map, rect, chunk, 2);
}

static void render_prop_layer_3(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_props(map, rect, chunk, 3);
}

static void render_bg_tiles(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_chunk_tiles(map, rect, chunk, false);
}

static void render_fg_tiles(map_t *map, const irect2_t *rect, tile_chunk_t *chunk) {
  render_chunk_tiles(map, rect, chunk, true);
}

static void render_water_plane(map_t *map, const irect2_t *rect, uint8_t layer) {
  if (map->water_line == FLT_MAX)
    return;
  int32_t y = map->water_line;
  float p = (y - map->camera_y) * (8.0/60.0);
  if (layer == 1 && y + p < rect->y1) {
    color_t color = map->water_color;
    if (color.a == 0)
      color = RGBA32(0, 0, 120, 128);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(color);
    rdpq_fill_rectangle(0, MAX(y + p - rect->y0, 0), display_get_width(), display_get_height());
  }
  y -= rect->y0;
  float y0 = y;
  float y1 = y;
  if (layer == 0)
    y0 -= p;
  else
    y1 += p;

  if (y0 < display_get_height() && y1 >= 0) {
    float dwidth = display_get_width();
    float w = dwidth * 0.5;
    float s0 = fmodf(rect->x0 * 0.5, 16.0f);
    float s1 = s0 + w;
    float bs0 = s0;
    float bs1 = s1;
    float z0;
    float z1;
    if (layer == 0) {
      s0 -= 8;
      s1 += 8;
      z0 = 0.5+64.0/(DEPTH_BOUND*2.0);
      z1 = 0.5f;
    } else {
      bs0 += 8;
      bs1 -= 8;
      z0 = 0.5f;
      z1 = 0.5-64.0/(DEPTH_BOUND*2.0);
    }
    float cw = (w + 16.0) / w;
    const float verts[][6] = {
      {      0, y0, z0,  s0, 0,  1.0 },
      { dwidth, y0, z0,  s1, 0,  1.0 },
      {      0, y1, z1, bs0, 16, cw },
      { dwidth, y1, z1, bs1, 16, cw },
    };

    rdpq_mode_push();
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_persp(true);
    rdpq_mode_zbuf(true, true);
    rdpq_mode_end();
    color_t color = map->water_color;
    if (color.a != 0) {
      color.a = (((uint32_t) color.a) * 3) >> 2;
      rdpq_set_prim_color(color);
    } else {
      rdpq_set_prim_color(RGBA32(255, 255, 255, 192));
    }
    rdpq_sprite_upload(TILE0, water_top[(map->frame_counter >> 3) % 3], &(rdpq_texparms_t) {
        .s.repeats = REPEAT_INFINITE,
    });
    rdpq_triangle(&TRIFMT_ZBUF_TEX, verts[0], verts[2], verts[1]);
    rdpq_triangle(&TRIFMT_ZBUF_TEX, verts[3], verts[2], verts[1]);
    rdpq_mode_pop();
  }

  if (layer == 1 && y1 < display_get_height() && y1 + 24 >= 0) {
    float w = display_get_width() * 0.5;
    float scroll = (w + 16.0) / w;
    sprite_t *water = water_side[(map->frame_counter >> 3) % 3];
    surface_t surf = sprite_get_pixels(water);
    rdpq_tex_upload(TILE0, &surf, &(rdpq_texparms_t) {
        .s.repeats = REPEAT_INFINITE,
        });
    int32_t s = (int32_t) (rect->x0 * scroll) & (32-1);
    rdpq_mode_push();
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    color_t color = map->water_color;
    if (color.a != 0) {
      color.a = (((uint32_t) color.a) * 3) >> 2;
      rdpq_set_prim_color(color);
    } else {
      rdpq_set_prim_color(RGBA32(255, 255, 255, 192));
    }
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_end();
    rdpq_texture_rectangle(TILE0, 0, y1 - 8, display_get_width(), y1 + 24, s, 0);
    rdpq_mode_pop();
  }
}

void render_sprite(actor_t *actor, const irect2_t *rect) {
  actor_sprite_t *sprite = (actor_sprite_t *) actor;
  bool standard;

  if (F2I(sprite->scale_x) == 0 || F2I(sprite->scale_y) == 0)
    return;

  if (!sprite_in_rect(sprite, rect))
    return;

  sprite_t *image = sprite->anim.image;
  tiles_desc_t *tiles = sprite->anim.tiles;
  int32_t offset_x = tiles->offset_x;
  int32_t offset_y = tiles->offset_y;
  float x, y;
  rdpq_blitparms_t parms = {
    .height = tiles->frame_height,
    .theta = -world_get_actor_angle(actor),
    .cx = -offset_x,
    .cy = -offset_y,
  };
  if (tiles->xmask) {
    uint32_t tid = sprite->anim.frame;
    parms.s0 = (tid & tiles->xmask) << tiles->xshift;
    parms.width = tiles->frame_width;
    parms.t0 = (tid & ~tiles->xmask) << tiles->yshift;
  } else {
    parms.t0 = parms.height * sprite->anim.frame;
  }

  world_get_actor_position(actor, &x, &y);
  standard = F2I(parms.theta) != 0;
  if (actor->flags & AF_FLIPX) {
    parms.flip_x = true;
    standard = true;
  }
  if (actor->flags & AF_FLIPY) {
    parms.flip_y = true;
    standard = true;
  }
  if (actor->flags & AF_FLIPD) {
    parms.flip_x = !parms.flip_x;
    parms.theta += M_PI*0.5;
    standard = true;
  }

  if (sprite->scale_x != 1.0 || sprite->scale_y != 1.0) {
    parms.scale_x = sprite->scale_x;
    parms.scale_y = sprite->scale_y;
    standard = true;
  }

  rdpq_mode_begin();
  if (standard) {
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
  } else {
    rdpq_set_mode_copy(true);
  }
  rdpq_mode_zoverride(true, 0.5, 0);
  rdpq_mode_end();
  rdpq_sprite_blit(image,
      (int32_t)roundf(x) - rect->x0,
      (int32_t)roundf(y) - rect->y0,
      &parms);
}

static void setup_scene_gl(const irect2_t *rect) {
  if (frame_used_gl)
    return;
  frame_used_gl = true;
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(-rect->x0, -rect->y0, 0);
  glLightfv(GL_LIGHT0, GL_POSITION, (GLfloat[4]) { 0.f, 200.f, -100.f, 1.f });
}

static void actor_model_gl_rotate(actor_model_t *model) {
  float pitch = world_get_actor_angle(&model->actor);
  if (model->yaw)
    glRotatef(model->yaw, 0, 1, 0);
  if (pitch)
    glRotatef(-pitch * RAD2DEGF, 1, 0, 0);
  if (model->roll)
    glRotatef(model->roll, 0, 0, 1);
}

static void actor_model_gl_unrotate(actor_model_t *model) {
  float pitch = world_get_actor_angle(&model->actor);
  if (model->roll)
    glRotatef(-model->roll, 0, 0, 1);
  if (pitch)
    glRotatef(pitch * RAD2DEGF, 1, 0, 0);
  if (model->yaw)
    glRotatef(-model->yaw, 0, 1, 0);
}

static void setup_actor_model_gl(actor_model_t *model, const irect2_t *rect) {
  gl_context_begin();
  setup_scene_gl(rect);
  glBindTexture(GL_TEXTURE_2D, model->gltex);
  glPushMatrix();

  float zscale;
  float x, y, z = model->z;
  if (z)
    zscale = powf(2.0, z * Z_SCALE_FACTOR);
  else
    zscale = 1.0;
  world_get_actor_position(&model->actor, &x, &y);
  glTranslatef(x, y, z);
  glScalef(model->scale_x * zscale, model->scale_y * zscale, model->scale_z);
}

static void finish_actor_model_gl(actor_model_t *model) {
  glPopMatrix();
  gl_context_end();
}

void render_spaceship(actor_t *actor, const irect2_t *rect) {
  spaceship_t *ship = (spaceship_t *) actor;
  setup_actor_model_gl(&ship->model, rect);
  actor_model_gl_rotate(&ship->model);
  glCallList(ship->model.list);

  if (ship->flags & SPACESHIP_THRUST_BACK) {
    static const float THRUST_BACK_POSITIONS[][3] = {
      {  54.0364,  1.5395 ,142.516 },
      { -54.0364, 1.5395, 142.516 },
    };
    for (size_t i = 0; i < COUNT_OF(THRUST_BACK_POSITIONS); i++) {
      const float *v = THRUST_BACK_POSITIONS[i];
      glPushMatrix();
      glTranslatef(v[0], v[1], v[2]);
      actor_model_gl_unrotate(&ship->model);
      glBindTexture(GL_TEXTURE_2D, ship->thrust_tex);
      glCallList(ship->thrust_list);
      glPopMatrix();
    }
  }
  if (ship->flags & SPACESHIP_THRUST_BOT) {
    static const float THRUST_BOTTOM_POSITIONS[][3] = {
      {   0, -26, -12.8376, },
      { -50, -26,  56.8522, },
      {  50, -26,  56.8522, }
    };
    for (size_t i = 0; i < COUNT_OF(THRUST_BOTTOM_POSITIONS); i++) {
      const float *v = THRUST_BOTTOM_POSITIONS[i];
      glPushMatrix();
      glTranslatef(v[0], v[1], v[2]);
      actor_model_gl_unrotate(&ship->model);
      glRotatef(-90, 0, 0, 1);
      glCallList(ship->thrust_list);
      glBindTexture(GL_TEXTURE_2D, ship->thrust_tex);
      glPopMatrix();
    }
  }
  finish_actor_model_gl(&ship->model);
}

void render_submarine(actor_t *actor, const irect2_t *rect) {
  submarine_t *sub = (submarine_t *) actor;

  setup_actor_model_gl(&sub->model, rect);
  actor_model_gl_rotate(&sub->model);

  glPushMatrix();
  glEnable(GL_BLEND);
  glEnable(GL_ALPHA_TEST);
  glRotatef(sub->propeller_spin, 0, 0, 1);
  glBindTexture(GL_TEXTURE_2D, sub->propeller_gltex);
  glCallList(sub->model.list + 1);
  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glPopMatrix();

  glBindTexture(GL_TEXTURE_2D, sub->model.gltex);
  glCallList(sub->model.list);

  finish_actor_model_gl(&sub->model);
}

static const int FADE_RECT_SIZE = 40;

void render_screenwipe_to_color(color_t color, float progress) {
  int rect_width = progress * FADE_RECT_SIZE;
  rdpq_set_mode_fill(color);
  int row = 0;
  int32_t screen_width = display_get_width();
  int32_t screen_height = display_get_height();
  for (int y = 0; y < screen_height; y += FADE_RECT_SIZE) {
    int xs = (row & 1) ? -FADE_RECT_SIZE/2 : 0;
    for (int x = xs; x < screen_width; x += FADE_RECT_SIZE) {
      rdpq_fill_rectangle(x, y, x+rect_width, y+FADE_RECT_SIZE);
    }
    row++;
  }
}

void render_screenwipe_from_grab(surface_t *grab, float progress) {
  int rect_width = (1.0 - progress) * FADE_RECT_SIZE;
  if (!rect_width)
    return;
  rdpq_set_mode_copy(false);
  int row = 0;
  int32_t screen_width = display_get_width();
  int32_t screen_height = display_get_height();
  rdpq_blitparms_t parms = {};
  for (int y = 0; y < screen_height; y += FADE_RECT_SIZE) {
    int xs = (row & 1) ? -FADE_RECT_SIZE/2 : 0;
    parms.t0 = y;
    parms.height = MIN(FADE_RECT_SIZE, screen_height - y);
    for (int x = xs; x < screen_width; x += FADE_RECT_SIZE) {
      parms.s0 = x;
      parms.width = MIN(rect_width, screen_width - x);
      if (parms.s0 < 0) {
        parms.width += parms.s0;
        parms.s0 = 0;
      }
      if (parms.width < 0)
        continue;
      rdpq_tex_blit(grab, x, y, &parms);
    }
    row++;
  }
}

void render_init(void) {
  rdpq_init();
  //rdpq_debug_start();
  //rdpq_debug_log(true);
  gl_init();

  {
    rdpq_font_t *font;
    const rdpq_fontstyle_t style = { FONT_COLOR };

    font = rdpq_font_load("rom:/fonts/pixeltype.font64");
    rdpq_font_style(font, STYLE_PLAIN, &style);
    rdpq_text_register_font(FONT_SMALL, font);

    font = rdpq_font_load("rom:/fonts/blocktopia.font64");
    rdpq_font_style(font, STYLE_PLAIN, &style);
    rdpq_text_register_font(FONT_MED, font);

    font = rdpq_font_load("rom:/fonts/STV5730A.font64");
    rdpq_font_style(font, STYLE_PLAIN, &style);
    rdpq_text_register_font(FONT_LARGE, font);
  }

  water_side[0] = sprite_load("rom:/fg/water-t1.sprite");
  water_side[1] = sprite_load("rom:/fg/water-t2.sprite");
  water_side[2] = sprite_load("rom:/fg/water-t3.sprite");
  water_top[0] = sprite_load("rom:/fg/water-surf1.sprite");
  water_top[1] = sprite_load("rom:/fg/water-surf2.sprite");
  water_top[2] = sprite_load("rom:/fg/water-surf3.sprite");

  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (GLfloat[4]) { .2f, .2f, .2f, 1.f });
  glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

  float light_radius = 1600.0f;

  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, (GLfloat[4]) { 1.f, 1.f, 1.f, 1.f });
  glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 2.0f/light_radius);
  glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 1.0f/(light_radius*light_radius));

  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, (GLfloat[4]) { 1.f, 1.f, 1.f, 1.f });

  glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_COLOR_MATERIAL);
  glEnable(GL_NORMALIZE);
  glEnable(GL_DEPTH_TEST);
  //glDepthFunc(GL_LESS_INTERPENETRATING_N64);
  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CCW);
  glEnable(GL_LIGHTING);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  //glHint(GL_MULTISAMPLE_HINT_N64, GL_FASTEST);
  glDisable(GL_MULTISAMPLE_ARB);
  glAlphaFunc(GL_GREATER, 0.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DITHER);
}

void render_setup(void) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, display_get_width(), display_get_height(), 0, -DEPTH_BOUND, DEPTH_BOUND);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void render_shadow_print(const rdpq_textparms_t *parms, uint8_t font, float x0, float y0, const char *text) {
  render_shadow_printn(parms, font, x0, y0, text, strlen(text));
}

void render_shadow_printn(const rdpq_textparms_t *parms, uint8_t font_id, float x0, float y0, const char *text, size_t len) {
  rdpq_font_t *font = (rdpq_font_t *) rdpq_text_get_font(font_id);
  rdpq_font_style(font, STYLE_PLAIN, &(rdpq_fontstyle_t) { RGBA32(0, 0, 0, 192) });
  rdpq_text_printn(parms, font_id, x0+1, y0+1, text, len);
  rdpq_font_style(font, STYLE_PLAIN, &(rdpq_fontstyle_t) { FONT_COLOR });
  rdpq_text_printn(parms, font_id, x0, y0, text, len);
}

void render_shadow_printf(const rdpq_textparms_t *parms, uint8_t font, float x0, float y0, const char *fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  render_shadow_printn(parms, font, x0, y0, buf, len);
}

void render_transitions(map_t *map) {
    switch (map->fade) {
    case FADE_OUT_COLOR:
    case FADE_INOUT_COLOR:
      {
        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_end();
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        color_t color = map->fade_color;
        color.a = 255 - (int) (255 * ease_quad_inout(fade));
        rdpq_set_prim_color(color);
        rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
      }
      break;
    case FADE_OUT_WIPE:
    case FADE_INOUT_WIPE:
      {
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        render_screenwipe_to_color(map->fade_color, 1.f - fade);
      }
      break;
    case FADE_IN_WIPE:
      {
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        render_screenwipe_to_color(map->fade_color, fade);
      }
      break;
    case FADE_IN_COLOR:
      {
        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_end();
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        color_t color = map->fade_color;
        color.a = (int) (255 * ease_quad_inout(fade));
        rdpq_set_prim_color(color);
        rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
      }
      break;
    case FADE_CROSS:
      if (screen_grab.buffer) {
        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_end();
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        color_t color = RGBA32(255, 255, 255, 255 * ease_quad_inout(fade));
        rdpq_set_prim_color(color);
        rdpq_tex_blit(&screen_grab, 0, 0, NULL);
      }
      break;
    case FADE_CROSS_WIPE:
      if (screen_grab.buffer) {
        float fade = ((float) map->fade_counter) * INV_FADE_LEN;
        render_screenwipe_from_grab(&screen_grab, 1.f - fade);
      }
      break;
    default:
      break;
    }
}

