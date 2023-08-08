#pragma once

#include "util.h"
#include <graphics.h>
#include <rdpq_text.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_SMALL 1
#define FONT_MED   2
#define FONT_LARGE 3

#define STYLE_PLAIN 0

typedef struct actor_s actor_t;
typedef struct map_s map_t;

void render_init(void);
void render_setup(void);
void render_scene(map_t *map);
void render_transitions(map_t *map);

void render_sprite(actor_t *actor, const irect2_t *rect);
void render_spaceship(actor_t *actor, const irect2_t *rect);
void render_submarine(actor_t *actor, const irect2_t *rect);

void render_shadow_print(const rdpq_textparms_t *parms, uint8_t font, float x0, float y0, const char *text);
void render_shadow_printn(const rdpq_textparms_t *parms, uint8_t font, float x0, float y0, const char *text, size_t len);
void render_shadow_printf(const rdpq_textparms_t *parms, uint8_t font, float x0, float y0, const char *fmt, ...);

void render_screenwipe_to_color(color_t color, float progress);
void render_screenwipe_from_grab(surface_t *grab, float progress);

#ifdef __cplusplus
}
#endif
