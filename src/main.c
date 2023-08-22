#include <libdragon.h>
#include <model64.h>

#include "main.h"
#include "assets.h"
#include "map.h"
#include "menu.h"
#include "render.h"
#include "sound.h"
#include "util.h"

#define INIT_MAP MAPS_INSIDE_SHIP

#define INTRO_LEN_FRAMES 100
#define MAX_CATCHUP_FRAMES 8

static void run_intro(void);
static global_state_t game_loop(void);
static void reset_fade(void);

static map_t current_map = { .header = NULL };

surface_t screen_grab = { .buffer = NULL };
surface_t zbuffer;
uint32_t screen_half_width;
uint32_t screen_half_height;

int main(void) {
  debug_init_usblog();
  debug_init_isviewer();
  controller_init();

  {
    uint32_t fcr31 = C1_FCR31();
    fcr31 &= ~(C1_ENABLE_OVERFLOW | C1_ENABLE_UNDERFLOW);
    C1_WRITE_FCR31(fcr31);
  }

  {
    int ret = dfs_init(DFS_DEFAULT_LOCATION);
    assertf(ret == DFS_ESUCCESS, "failed to initialize dfs");
  }

  render_init();
  sound_init();
  run_intro();
  const resolution_t res = {428, 240, false};
  display_init(res, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_OFF);
  throttle_init(FPS, 0, 1);
  screen_half_width = display_get_width() >> 1;
  screen_half_height = display_get_height() >> 1;

  zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());

  render_setup();

  surface_t *screen = display_get();
  rdpq_attach_clear(screen, &zbuffer);
  rdpq_detach_show();

  throttle_wait();

  sound_play_music(MUS_OUTDOOR, 0, 0);

  global_state_t state = ST_MAIN_MENU;
  while (true) {
    switch (state) {
    case ST_MAIN_MENU:
      map_load(maps_paths[INIT_MAP], &current_map, MSF_PLAYER_CONTROL);
      current_map.map_id = INIT_MAP;
      state = main_menu_loop(&current_map);
      break;
    case ST_GAME:
      state = game_loop();
      break;
    case ST_PAUSE:
      state = pause_loop(&current_map);
      break;
    case ST_RESET:
      goto reset;
    default:
      break;
    }
  }

reset:
  audio_close();
  reset_fade();

  return 0;
}

static global_state_t game_loop(void) {
  int32_t lag_time = 0;
  surface_t *screen = NULL;

  while (true) {
    screen = display_get();
    rdpq_attach(screen, &zbuffer);
    render_scene(&current_map);
    render_transitions(&current_map);
    rdpq_detach_show();
    sound_tick();

    if (!throttle_wait()) {
      int32_t time_left = throttle_frame_time_left();
      if (time_left < 0) {
        lag_time += time_left;
      }
    }

    controller_scan();

    int catchup_frames = 0;
    while (true) {
      global_state_t next_state = map_tick(&current_map);
      sound_tick();
      if (next_state == ST_PAUSE || next_state == ST_RESET)
        grab_screen(screen);
      if (next_state == ST_NEW_MAP)
        map_transition(&current_map, screen);
      else if (next_state != ST_GAME)
        return next_state;
      if (lag_time > (FRAME_TICKS * 2)) {
        lag_time -= FRAME_TICKS;
        controller_scan();
        if (++catchup_frames >= MAX_CATCHUP_FRAMES) {
          lag_time = 0;
          break;
        }
      } else {
        break;
      }
    }
  }
}

void change_vid_mode(bool wide, map_t *map) {
  resolution_t res = {wide ? 428 : 320, 240, false};
  rdpq_sync_pipe();
  rspq_wait();
  display_close();
  surface_free(&zbuffer);
  display_init(res, DEPTH_16_BPP, 3, GAMMA_NONE, wide ? ANTIALIAS_OFF : ANTIALIAS_RESAMPLE);
  zbuffer = surface_alloc(FMT_RGBA16, res.width, res.height);
  screen_half_width = res.width >> 1;
  render_setup();
  if (screen_grab.buffer)
    surface_free(&screen_grab);
  screen_grab = surface_alloc(FMT_RGBA16, res.width, res.height);
  if (screen_grab.buffer) {
    rdpq_attach_clear(&screen_grab, &zbuffer);
    rdpq_sync_pipe();
    rspq_wait();
    render_scene(map);
    rdpq_detach_wait();
    rdpq_sync_pipe();
    rdpq_sync_tile();
  }
}

static void draw_intro_gradient(float y) {

#define COL1 0.98, 0.22, 1.00, 1.0
#define COL2 0.00, 0.72, 0.74, 1.0

  float screenquads[][6] = {
    { 0,   0, COL1 },  { 640,   0, COL1, }, { 0, 240, COL2 }, { 640, 240, COL2 },
    { 0, 240, COL2 },  { 640, 240, COL2, }, { 0, 480, COL1 }, { 640, 480, COL1 },
  };
  for (int i = 0; i < 8; i++)
    screenquads[i][1] += y;
  for (int i = 0; i < 8; i+=4) {
    rdpq_triangle(&TRIFMT_SHADE, screenquads[i+0], screenquads[i+1], screenquads[i+2]);
    rdpq_triangle(&TRIFMT_SHADE, screenquads[i+3], screenquads[i+2], screenquads[i+1]);
  }
}

static void run_intro(void) {
  int counter = 0;
  float fade = 0.0;

  display_init(RESOLUTION_640x480, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_OFF);
  throttle_init(30, 0, 0);
  sprite_t *summer = sprite_load("rom:/ui/summer.sprite");
  while (counter < INTRO_LEN_FRAMES) {
    if (exception_reset_time() > 0) {
      surface_t *screen = display_get();
      memset(screen, 0, screen->stride * screen->height);
      display_show(screen);
      while(1) {}
    }
    if (counter < 8)
      fade = counter / 8.0;
    else if (counter >= INTRO_LEN_FRAMES - 8)
      fade = 1.0 - (counter - (INTRO_LEN_FRAMES - 8)) / 8.0;
    else {
      controller_scan();
      pad_t kdown = get_keys_down();
      if (kdown.c[0].A || kdown.c[0].B || kdown.c[0].start)
        counter = INTRO_LEN_FRAMES - 8;
    }
    surface_t *screen = display_get();
    rdpq_attach(screen, NULL);
    rdpq_set_mode_standard();
    rdpq_set_prim_color(RGBA32(0, 0, 0, 255 * fade));
    rdpq_mode_combiner(RDPQ_COMBINER1((SHADE,0,PRIM_ALPHA,0), (0,0,0,1)));
    float grad_y = (counter % 30) * 16;
    draw_intro_gradient(grad_y);
    draw_intro_gradient(grad_y - 480);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM_ALPHA,0), (0,0,0,TEX0)));
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_sprite_blit(summer, 320 - summer->width / 2.0, 240 - summer->height / 2.0, NULL);
    rdpq_detach_show();
    sound_tick();
    throttle_wait();
    sound_tick();
    counter += 1;
  }
  rspq_wait();
  sprite_free(summer);
  display_close();
}

void grab_screen(surface_t *screen) {
  if (screen_grab.buffer)
    surface_free(&screen_grab);
  if (!screen)
    return;
  screen_grab = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
  if (screen_grab.buffer) {
    rdpq_attach(&screen_grab, NULL);
    rdpq_set_mode_standard();
    rdpq_set_mode_copy(true);
    rdpq_tex_blit(screen, 0, 0, NULL);
    rdpq_detach();
  }
}

static void reset_fade(void) {
  if (!screen_grab.buffer) {
    surface_t *screen = display_get();
    memset(screen, 0, (int)screen->stride * (int)screen->height);
    display_show(screen);
    return;
  }

  throttle_init(60, 0, 0);

  color_t color = RGBA32(0, 0, 0, 255);
  int reset_frames = (TICKS_FROM_MS(500) - exception_reset_time()) / TICKS_FROM_US(16667);
  for (int i = 0; i < reset_frames; i++) {
    float progress = ((i + 1) / (float) reset_frames);
    surface_t *screen = display_get();
    rdpq_attach(screen, NULL);
    rdpq_set_mode_copy(true);
    rdpq_tex_blit(&screen_grab, 0, 0, NULL);
    render_screenwipe_to_color(color, progress);
    rdpq_detach_show();
    throttle_wait();
  }

  rspq_wait();
  if (screen_grab.buffer)
    surface_free(&screen_grab);
  display_close();
}
