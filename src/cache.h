#pragma once

#include <GL/gl.h>
#include <graphics.h>
#include <model64.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tiles_desc_s tiles_desc_t;

sprite_t *sprite_pool_load(int id);
sprite_t *sprite_pool_load_gl(int id, GLuint *tex);
void sprite_pool_unload(sprite_t *sprite);

GLuint model_pool_load(int id, size_t bufsize);
void model_pool_unload(GLuint start_list);

tiles_desc_t *tileset_pool_load(int id);
void tileset_pool_unload(tiles_desc_t *tiles);

#ifdef __cplusplus
}
#endif
