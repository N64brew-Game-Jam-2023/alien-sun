#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sound_init(void);
void sound_tick(void);

float sound_get_fx_volume(void);
float sound_get_music_volume(void);
void sound_set_fx_volume(float vol);
void sound_set_music_volume(float vol);

typedef enum {
  SP_RESTART    = 1<<0,
  SP_ALL_SOUNDS = 1<<1,
} music_play_flags_t;

void sound_play_music(uint16_t id, float fade_secs, unsigned int flags);
void sound_set_music_gain(float vol);

void sound_set_listener_pos(float x, float y);
void sound_play_fx(uint16_t id, float x, float y, int priority, int *ch);
void sound_play_fx_global(uint16_t id, int priority, int *ch);
void sound_update_position(int ch, float x, float y);
void sound_release_channel(int *ptr);
void sound_pause_fx(void);
void sound_resume_fx(void);

#ifdef __cplusplus
}
#endif
