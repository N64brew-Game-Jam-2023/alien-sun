#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FPS 60
#define INV_FPS (1.0/(float)60)
#define FRAME_TICKS (TICKS_PER_SECOND / FPS)
#define INV_FRAME_TICKS (1.0 / (float) (unsigned) FRAME_TICKS)

#define JOYSTICK_DEAD_ZONE 10
#define JOYSTICK_MAX (100 - JOYSTICK_DEAD_ZONE)

#define JOYSTICK_UI_DEAD_ZONE 32
#define JOYSTICK_UI_MAX (100 - JOYSTICK_UI_DEAD_ZONE)

typedef struct map_s map_t;
typedef struct surface_s surface_t;

typedef enum {
  ST_GAME,
  ST_MAIN_MENU,
  ST_PAUSE,
  ST_RESET,
  ST_NEW_MAP,
} global_state_t;

extern surface_t screen_grab;
extern surface_t zbuffer;
extern uint32_t screen_half_width;
extern uint32_t screen_half_height;

void grab_screen(surface_t *screen);
void change_vid_mode(bool wide, map_t *map);

#ifdef __cplusplus
}
#endif
