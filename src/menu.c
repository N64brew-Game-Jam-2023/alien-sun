#include "menu.h"
#include "assets.h"
#include "main.h"
#include "map.h"
#include "render.h"
#include "sound.h"
#include "util.h"

#include <libdragon.h>

#define GAME_URL "https://9nova.itch.io/aliensun"

#define PAD_PUSH (JOYSTICK_UI_DEAD_ZONE / 2)
#define VOL_TICK_INCR (3.0/100.0)

#define ROW_SPACING 16
#define COL_SPACING 18

#define FOREACH_MENU_ACTION \
  ENTRY(SPACER, NULL) \
  ENTRY(START, "Start") \
  ENTRY(RESUME, "Resume") \
  ENTRY(OPTIONS, "Options") \
  ENTRY(RESPAWN, "Respawn") \
  ENTRY(QUIT, "Quit") \
  ENTRY(ASPECT, "Screen Aspect") \
  ENTRY(SFX_VOL, "SFX Volume") \
  ENTRY(MUS_VOL, "Music Volume") \
  ENTRY(BACK, "Back")

typedef enum {
#define ENTRY(id, text) \
  MA_##id,
  FOREACH_MENU_ACTION
#undef ENTRY
} menu_action_t;

const char * const menu_texts[] = {
#define ENTRY(id, text) \
  text,
  FOREACH_MENU_ACTION
#undef ENTRY
};

static pad_t get_keys_down_stick_to_pad(void);
static int menu_move(int pos, pad_t *kdown, size_t len);
static int8_t get_x_push(struct SI_condat *con);

static int options_menu_tick(int pos, map_t *map, surface_t **screen);

static void render_center_menu(int32_t y, int pos, const menu_action_t *menu, size_t len);
static void render_options_menu(int32_t x_divider, int32_t y, int pos, const menu_action_t *menu, size_t len);
static void render_main_options_menu(int pos);

static const menu_action_t main_menu[] = { MA_START, MA_OPTIONS };
static const menu_action_t pause_menu[] = { MA_RESUME, MA_OPTIONS, MA_RESPAWN, MA_QUIT };
static const menu_action_t options_menu[] = { MA_ASPECT, MA_SFX_VOL, MA_MUS_VOL };
static const menu_action_t back_menu[] = { MA_BACK };

static int8_t joystick_x_direction = 0;
static int8_t joystick_y_direction = 0;

static int menu_sound_channel = -1;

global_state_t main_menu_loop(map_t *map) {
  global_state_t state = ST_GAME;
  unsigned long start = get_ticks();
  int pos = -1;
  float fade = 0.0;
  int options_pos = -1;
  uint32_t orig_state_flags = map->state_flags;
  map->state_flags &= ~MSF_PLAYER_CONTROL;
  sprite_t *title = sprite_load("rom:/ui/title.sprite");
  surface_t *screen = NULL;

  while (true) {
    if (map_tick(map) == ST_RESET) {
      if (screen)
        grab_screen(screen);
      state = ST_RESET;
      goto done;
    }

    if (pos == -1) {
      fade = (get_ticks() - start) / (float) TICKS_FROM_MS(200);
      if (fade >= 1.0) {
        fade = 1.0;
        pos = 0;
      }
    } else if (options_pos >= 0) {
      options_pos = options_menu_tick(options_pos, map, &screen);
    } else if (pos >= 0) {
      controller_scan();
      pad_t kdown = get_keys_down_stick_to_pad();
      pos = menu_move(pos, &kdown, COUNT_OF(main_menu));
      if (kdown.c[0].A || kdown.c[0].start) {
        sound_play_fx_global(SFX_BLIP2, 0, &menu_sound_channel);
        switch (main_menu[pos]) {
        case MA_START: goto done;
        case MA_OPTIONS: options_pos = 0; break;
        default: break;
        }
      }
    }
    screen = display_get();
    rdpq_attach_clear(screen, &zbuffer);
    render_scene(map);
    if (options_pos >= 0) {
      render_main_options_menu(options_pos);
    } else {
      rdpq_set_mode_copy(true);
      rdpq_sprite_blit(title, screen_half_width - title->width / 2.0, screen_half_height - title->height - 18, NULL);
      rdpq_text_print(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = display_get_width() },
                      FONT_MED, 0, screen_half_height + 16, GAME_URL);
      render_center_menu(60, pos, main_menu, COUNT_OF(main_menu));
    }
    if (fade < 1.0) {
      rdpq_mode_begin();
      rdpq_set_mode_standard();
      rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
      rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
      rdpq_mode_end();
      rdpq_set_prim_color(RGBA32(0, 0, 0, 255 - 255 * ease_quad_inout(fade)));
      rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
    }
    rdpq_detach_show();
    sound_tick();
    throttle_wait();
    sound_tick();
  }
done:
  map->state_flags = orig_state_flags;
  sprite_free(title);
  return state;
}

global_state_t pause_loop(map_t *map) {
  global_state_t state;
  int pos = 0;
  int options_pos = -1;

  sound_set_music_gain(0.5);
  sound_pause_fx();

  while (true) {
    if (exception_reset_time() > 0)
      return ST_RESET;

    if (options_pos >= 0) {
      options_pos = options_menu_tick(options_pos, map, NULL);
    } else if (pos >= 0) {
      controller_scan();
      pad_t kdown = get_keys_down_stick_to_pad();
      pos = menu_move(pos, &kdown, COUNT_OF(pause_menu));
      if (kdown.c[0].B || kdown.c[0].start) {
        state = ST_GAME;
        goto done;
      } else if (kdown.c[0].A) {
        sound_play_fx_global(SFX_BLIP2, 0, &menu_sound_channel);
        switch (pause_menu[pos]) {
        case MA_RESUME: state = ST_GAME; goto done; break;
        case MA_OPTIONS: options_pos = 0; break;
        case MA_RESPAWN: state = ST_GAME; map->state_flags |= MSF_FORCE_RESPAWN; goto done; break;
        case MA_QUIT: state = ST_MAIN_MENU; goto done; break;
        default: break;
        }
      }
    }
    surface_t *screen = display_get();
    rdpq_attach(screen, &zbuffer);
    if (screen_grab.buffer) {
      rdpq_set_mode_copy(true);
      rdpq_tex_blit(&screen_grab, 0, 0, NULL);
    } else {
      sound_tick();
      render_scene(map);
    }
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 128));
    rdpq_mode_end();
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
    if (options_pos >= 0) {
      render_main_options_menu(options_pos);
    } else {
      rdpq_text_print(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = display_get_width() },
                      FONT_MED, 0, screen_half_height - 6, "Pause");
      render_center_menu(28, pos, pause_menu, COUNT_OF(pause_menu));
    }
    rdpq_detach_show();
    sound_tick();
    throttle_wait();
    sound_tick();
  }
done:
  if (screen_grab.buffer)
    surface_free(&screen_grab);
  sound_resume_fx();
  sound_set_music_gain(1.0);
  return state;
}

static int options_menu_tick(int pos, map_t *map, surface_t **screen) {
  controller_scan();
  pad_t kdown = get_keys_down_stick_to_pad();
  if (kdown.c[0].B || kdown.c[0].start) {
    sound_play_fx_global(SFX_BLIP, 0, &menu_sound_channel);
    return -1;
  }
  pos = menu_move(pos, &kdown, COUNT_OF(options_menu) + 1);
  if (kdown.c[0].A && pos == COUNT_OF(options_menu)) {
    sound_play_fx_global(SFX_BLIP2, 0, &menu_sound_channel);
    return -1;
  }
  pad_t kpressed = get_keys_pressed();
  switch (pos < COUNT_OF(options_menu) ? options_menu[pos] : MA_BACK) {
    case MA_ASPECT:
      if (kdown.c[0].A || kdown.c[0].left || kdown.c[0].right) {
        sound_play_fx_global(SFX_BLIP2, 0, &menu_sound_channel);
        change_vid_mode(display_get_width() == 320, map);
        if (screen)
          *screen = NULL;
      }
      break;
    case MA_SFX_VOL:
      {
        int8_t push = get_x_push(&kpressed.c[0]);
        if (push) {
          float vol = sound_get_fx_volume() + push * (VOL_TICK_INCR / JOYSTICK_UI_MAX);
          sound_set_fx_volume(CLAMP(vol, 0.0, 1.0));
        }
      }
      break;
    case MA_MUS_VOL:
      {
        int8_t push = get_x_push(&kpressed.c[0]);
        if (push) {
          float vol = sound_get_music_volume() + push * (VOL_TICK_INCR / JOYSTICK_UI_MAX);
          sound_set_music_volume(CLAMP(vol, 0.0, 1.0));
        }
      }
      break;
    default:
      break;
  }
  return pos;
}

static int8_t get_x_push(struct SI_condat *con) {
  int8_t push = 0;
  if (con->left)
    push -= PAD_PUSH;
  if (con->right)
    push += PAD_PUSH;
  if (con->x < -JOYSTICK_UI_DEAD_ZONE) {
    if (push < 0)
      push = MIN(push, (int8_t) con->x + JOYSTICK_UI_DEAD_ZONE);
    else
      push += con->x + JOYSTICK_UI_DEAD_ZONE;
  }
  else if (con->x > +JOYSTICK_UI_DEAD_ZONE) {
    if (push > 0)
      push = MAX(push, (int8_t) con->x - JOYSTICK_UI_DEAD_ZONE);
    else
      push += con->x - JOYSTICK_UI_DEAD_ZONE;
  }
  return push;
}

static pad_t get_keys_down_stick_to_pad(void) {
  pad_t kpressed = get_keys_pressed();
  pad_t kdown = get_keys_down();
  if (kpressed.c[0].x < -JOYSTICK_UI_DEAD_ZONE) {
    if (joystick_x_direction == 0)
      kdown.c[0].left = true;
    joystick_x_direction = -1;
  } else if (kpressed.c[0].x > +JOYSTICK_UI_DEAD_ZONE) {
    if (joystick_x_direction == 0)
      kdown.c[0].right = true;
    joystick_x_direction = +1;
  } else {
    joystick_x_direction = 0;
  }
  if (kpressed.c[0].y < -JOYSTICK_UI_DEAD_ZONE) {
    if (joystick_y_direction == 0)
      kdown.c[0].down = true;
    joystick_y_direction = -1;
  } else if (kpressed.c[0].y > +JOYSTICK_UI_DEAD_ZONE) {
    if (joystick_y_direction == 0)
      kdown.c[0].up = true;
    joystick_y_direction = +1;
  } else {
    joystick_y_direction = 0;
  }
  return kdown;
}

static int menu_move(int pos, pad_t *kdown, size_t len) {
  if (kdown->c[0].up)
    DEC_WRAP(pos, len);
  if (kdown->c[0].down)
    INC_WRAP(pos, len);
  if (kdown->c[0].up || kdown->c[0].down)
    sound_play_fx_global(SFX_BLIP, 0, &menu_sound_channel);
  return pos;
}

static void render_center_caret(uint32_t width, int32_t y) {
  width >>= 1;
  rdpq_text_print(NULL, FONT_MED, screen_half_width - width, y, ">");
  rdpq_text_print(NULL, FONT_MED, screen_half_width + width, y, "<");
}

static void render_center_menu(int32_t y, int pos, const menu_action_t *menu, size_t len) {
  const rdpq_textparms_t hcenter = { .align = ALIGN_CENTER, .width = display_get_width() };
  y += screen_half_height;
  FOREACH_ARRAYN(action, menu, len) {
    rdpq_text_print(&hcenter, FONT_MED, 0, y, menu_texts[*action]);
    if (action - menu == pos)
      render_center_caret(120, y);
    y += 19;
  }
}

#define OPTVAL_BUFSIZE 32

static const char *print_option_value(menu_action_t action, char buf[static OPTVAL_BUFSIZE]) {
  switch (action) {
    case MA_ASPECT:
      return display_get_width() == 320 ? "4:3" : "16:9";
    case MA_SFX_VOL:
      snprintf(buf, OPTVAL_BUFSIZE, "%d", (int) roundf(sound_get_fx_volume() * 100.0));
      break;
    case MA_MUS_VOL:
      snprintf(buf, OPTVAL_BUFSIZE, "%d", (int) roundf(sound_get_music_volume() * 100.0));
      break;
    default:
      return NULL;
  }
  return buf;
}

static void render_options_menu(int32_t x_divider, int32_t y, int pos, const menu_action_t *menu, size_t len) {
  char buf[OPTVAL_BUFSIZE];
  const rdpq_textparms_t col1 = { .align = ALIGN_RIGHT, .width = screen_half_width + x_divider };
  int32_t col2_x = x_divider + COL_SPACING;

  y += display_get_height() >> 1;
  FOREACH_ARRAYN(action, menu, len) {
    rdpq_text_print(&col1, FONT_MED, 0, y, menu_texts[*action]);
    if (action - menu == pos)
      render_center_caret(192, y);
    const char *text = print_option_value(*action, buf);
    if (text)
        rdpq_text_print(&col1, FONT_MED, col2_x, y, text);
    y += ROW_SPACING;
  }
  render_center_menu(60, pos - len, back_menu, COUNT_OF(back_menu));
}

static void render_main_options_menu(int pos) {
  rdpq_text_print(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = display_get_width() },
                  FONT_MED, 0, screen_half_height-70, "Options");
  render_options_menu(20, -40, pos, options_menu, COUNT_OF(options_menu));
}
