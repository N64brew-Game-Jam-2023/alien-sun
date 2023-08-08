#include <asset.h>
#include <inttypes.h>
#include <string.h>
#include <model64.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <debug.h>

#include "cache.h"
#include "sprite.h"
#include "actors.h"

#define TILES_MAGIC 0x54494c45 // TILE

typedef struct cached_file_s cached_file_t;
typedef SLIST_HEAD(cache_pool_s, cached_file_s) cache_pool_t;

cached_file_t *cached_file_open(cache_pool_t *pool, uint16_t id, const char * const * const paths, uintptr_t (*load_func)(const char *, uintptr_t arg), uintptr_t arg);
void cached_file_close(cache_pool_t *pool, uintptr_t ptr, void (*unload_func)(uintptr_t , uintptr_t arg));

struct cached_file_s {
  uintptr_t data;
  uint32_t refcount;
  uintptr_t arg;
  SLIST_ENTRY(cached_file_s) pool;
  uint16_t id;
};

cached_file_t *cached_file_open(cache_pool_t *pool, uint16_t id, const char * const * const paths, uintptr_t (*load_func)(const char *, uintptr_t arg), uintptr_t arg) {
  cached_file_t *link;
  SLIST_FOREACH(link, pool, pool) {
    if (id == link->id) {
      link->refcount++;
      return link;
    }
  }

  uintptr_t data = load_func(paths[id], arg);
  link = malloc(sizeof(cached_file_t));
  assertf(link != NULL, "out of memory");
  link->refcount = 1;
  link->data = data;
  link->arg = arg;
  link->id = id;
  SLIST_INSERT_HEAD(pool, link, pool);
  return link;
}

void cached_file_close(cache_pool_t *pool, uintptr_t ptr, void (*unload_func)(uintptr_t , uintptr_t arg)) {
  cached_file_t *link, *prev = NULL;
  SLIST_FOREACH(link, pool, pool) {
    if (link->data == ptr) {
      link->refcount--;
      if (link->refcount == 0) {

        if (SLIST_FIRST(pool) == link)
          pool->slh_first = link->pool.sle_next;
        else
          prev->pool.sle_next = link->pool.sle_next;

        unload_func(ptr, link->arg);
        free(link);
      }
      return;
    }
    prev = link;
  }
  assertf(0, "value 0x%"PRIuPTR" already removed from pool", ptr);
}

static cache_pool_t sprite_pool;
extern const char * const gfx_paths[];

static uintptr_t cache_sprite(const char *filename, uintptr_t arg) {
  return (uintptr_t) sprite_load(filename);
}

static void uncache_sprite(uintptr_t sprite, uintptr_t arg) {
  if (arg) {
    GLuint tex = arg;
    glDeleteTextures(1, &tex);
  }
  sprite_free((void *) sprite);
}

sprite_t *sprite_pool_load(int id) {
  return (sprite_t *) cached_file_open(&sprite_pool, id, gfx_paths, cache_sprite, 0)->data;
}

sprite_t *sprite_pool_load_gl(int id, GLuint *tex) {
  cached_file_t *link = cached_file_open(&sprite_pool, id, gfx_paths, cache_sprite, 0);
  if (!link->arg) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glSpriteTextureN64(GL_TEXTURE_2D, (sprite_t *) link->data, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    link->arg = tex;
  }
  *tex = link->arg;
  return (sprite_t *) link->data;
}

void sprite_pool_unload(sprite_t *sprite) {
  cached_file_close(&sprite_pool, (uintptr_t) sprite, uncache_sprite);
}

static cache_pool_t model_pool;
extern const char * const model_paths[];

static uintptr_t cache_model(const char *filename, uintptr_t arg) {
  model64_t *model = model64_load(filename);
  uint32_t meshes = model64_get_mesh_count(model);
  size_t bufsize = (size_t) arg;
  assertf(bufsize == meshes, "expected space for %"PRIu32" display lists, got %zu", meshes, bufsize);
  GLuint list = glGenLists(meshes);
  for (uint32_t i = 0; i < meshes; i++) {
    glNewList(list+i, GL_COMPILE);
    model64_draw_mesh(model64_get_mesh(model, i));
    glEndList();
  }
  model64_free(model);
  return list;
}

static void uncache_model(uintptr_t list, uintptr_t arg) {
  glDeleteLists((GLuint) list, (GLsizei) arg);
}

GLuint model_pool_load(int id, size_t bufsize) {
  return cached_file_open(&model_pool, id, model_paths, cache_model, bufsize)->data;
}

void model_pool_unload(GLuint start_list) {
  cached_file_close(&model_pool, start_list, uncache_model);
}

static cache_pool_t tileset_pool;
extern const char * const tileset_paths[];

static uintptr_t cache_tileset(const char *filename, uintptr_t arg) {
  tiles_desc_t *tileset = asset_load(filename, NULL);
  assertf(tileset->magic == TILES_MAGIC, "%s not a valid tileset", filename);
  return (uintptr_t) tileset;
}

static void uncache_tileset(uintptr_t tileset, uintptr_t arg) {
  free((tiles_desc_t *) tileset);
}

tiles_desc_t *tileset_pool_load(int id) {
  return (tiles_desc_t *) cached_file_open(&tileset_pool, id, tileset_paths, cache_tileset, 0)->data;
}

void tileset_pool_unload(tiles_desc_t *tiles) {
  cached_file_close(&tileset_pool, (uintptr_t) tiles, uncache_tileset);
}
