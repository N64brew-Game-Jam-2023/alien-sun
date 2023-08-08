
#include <float.h>
#include <libdragon.h>

#include "assets.h"
#include "sound.h"
#include "util.h"

#define DEFAULT_SFX_VOL 1.0
#define DEFAULT_MUS_VOL 0.75

#define PAN_WIDTH 214.0
#define PAN_HEIGHT 120.0
#define INV_PAN_WIDTH (1.0/PAN_WIDTH)

#define FALLOFF_SQ_START ((PAN_WIDTH * PAN_WIDTH) + (PAN_HEIGHT * PAN_HEIGHT))
#define FALLOFF_SQ_END   (FALLOFF_SQ_START*4.0)
#define INV_FALLOFF_DIST (1.0/(FALLOFF_SQ_END - FALLOFF_SQ_START))

#define AUDIO_FREQ 22050
#define NUM_CHANNELS 20

#define CHANNEL_MUSIC 0
#define MAX_SFX_CHANNEL (NUM_CHANNELS - 1)

static xm64player_t xmplayer;

static void null_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking) {}

static wav64_t sfxs[NUM_SFX] = {
  [SFX_NONE] = { .wave = {
    .name = "silent", .bits = 8, .channels = 1, .frequency = AUDIO_FREQ,
    .len = 0, .loop_len = 0, .read = null_read
  } },
};

static float sfx_user_volume = DEFAULT_SFX_VOL;
static float music_user_volume = DEFAULT_MUS_VOL;

static float sfx_volume = DEFAULT_SFX_VOL;
static float music_gain = 1.0;

static float listener_x = 0.f;
static float listener_y = 0.f;

static float fade_vol = 1.0;
static float fade_step;

static mus_id_t current_music = MUS_NONE;
static mus_id_t pending_music = MUS_NONE;
static float music_fade_vol = 1.0;
static float music_fade_step;

static struct {
  sfx_id_t sound;
  int priority;
  float lvol;
  float rvol;
  float pos;
  float x;
  float y;
  int *ptr;
} current_sfxs[NUM_CHANNELS] = {};
int last_sfx_channel = MAX_SFX_CHANNEL - 1;
int ambient_channel = -1;

static int first_fx_channel(void) {
  return xmplayer.ctx ? xm64player_num_channels(&xmplayer) : 0;
}

#define FOREACH_SFX_CHAN(var) \
  for (int var = first_fx_channel(); var < MAX_SFX_CHANNEL; var++)

static float distance_atttenuation(float x, float y) {
  x -= listener_x;
  y -= listener_y;
  float dist_sq = (x * x) + (y * y);
  if (dist_sq >= FALLOFF_SQ_END)
    return 0.0;
  if (dist_sq <= FALLOFF_SQ_START)
    return 1.0;
  float r_sq = (dist_sq - FALLOFF_SQ_START) * INV_FALLOFF_DIST;
  return r_sq * r_sq;
}

static int unset_channel_pointer(void *ptr) {
  current_sfxs[*(int *)ptr].ptr = NULL;
  *(int *)ptr = -1;
  return 0;
}

static void stop_sfx(int ch) {
  if (mixer_ch_playing(ch)) {
    mixer_ch_stop(ch);
    current_sfxs[ch].sound = SFX_NONE;
    if (current_sfxs[ch].ptr) {
      unset_channel_pointer(current_sfxs[ch].ptr);
      mixer_remove_event(unset_channel_pointer, current_sfxs[ch].ptr);
    }
  }
}

static void sound_cb(short *buffer, size_t numsamples) {
  if (music_fade_step) {
    music_fade_vol -= music_fade_step;
    if (music_fade_vol <= 0) {
      music_fade_vol = 1.0;
      music_fade_step = 0.0;
      current_music = pending_music;
      int end_ch = 0;
      if (xmplayer.ctx) {
        end_ch = first_fx_channel();
        xm64player_close(&xmplayer);
      }
      if (current_music) {
        xm64player_open(&xmplayer, mus_paths[current_music]);
        for (int ch = end_ch; ch < xm64player_num_channels(&xmplayer); ch++)
          stop_sfx(ch);
        xm64player_play(&xmplayer, CHANNEL_MUSIC);
      }
    }
  }
  if (xmplayer.ctx) {
    xm64player_set_vol(&xmplayer, music_user_volume * music_fade_vol * music_gain);
  }
  if (fade_step) {
    fade_vol -= fade_step;
    if (fade_vol <= 0) {
      fade_vol = 1.0;
      fade_step = 0.0;
      FOREACH_SFX_CHAN(ch)
        stop_sfx(ch);
    }
    sfx_volume = sfx_user_volume * fade_vol;
  }
  FOREACH_SFX_CHAN(channel) {
    if (mixer_ch_playing(channel)) {
      float x = current_sfxs[channel].x;
      float lvol, rvol;
      if (x != FLT_MAX) {
        float y = current_sfxs[channel].y;
        float att = distance_atttenuation(x, y);
        if (att > 0.0) {
          if (x < listener_x) {
            lvol = att;
            rvol = MAX(1.0 - (listener_x - x) * INV_PAN_WIDTH, 0.0);
          } else if (x > listener_x) {
            rvol = att;
            lvol = MAX(1.0 - (x - listener_x) * INV_PAN_WIDTH, 0.0);
          } else {
            lvol = rvol = 1.0;
          }
        } else {
          lvol = rvol = 0.0;
        }
        current_sfxs[channel].lvol = lvol;
        current_sfxs[channel].rvol = rvol;
      } else {
        lvol = rvol = 1.0;
      }
      mixer_ch_set_vol(channel, lvol * sfx_volume, rvol * sfx_volume);
    }
  }
  mixer_poll(buffer, numsamples);
}

void sound_tick(void) {
  if (audio_can_write()) {
    short *buf = audio_write_begin();
    sound_cb(buf, audio_get_buffer_length());
    audio_write_end();
  }
}

float sound_get_fx_volume(void) {
  return sfx_user_volume;
}

float sound_get_music_volume(void) {
  return music_user_volume;
}

void sound_set_fx_volume(float vol) {
  //disable_interrupts();
  sfx_user_volume = vol;
  sfx_volume = sfx_user_volume * fade_vol;
  //enable_interrupts();
}

void sound_set_music_volume(float vol) {
  //disable_interrupts();
  music_user_volume = vol;
  if (xmplayer.ctx)
    xm64player_set_vol(&xmplayer, vol);
  //enable_interrupts();
}

void sound_play_music(uint16_t id, float fade_secs, unsigned int flags) {
  assertf(id < NUM_MUS, "invalid music id %d", id);
  //disable_interrupts();

  if (!(flags & SP_RESTART) && music_fade_step <= 0 && current_music == id)
      return;

  if (!current_music)
    fade_secs = 0;

  pending_music = id;
  music_fade_step = fade_secs > 0 ? fade_secs / (float) AUDIO_FREQ : 1.0;

  if (flags & SP_ALL_SOUNDS)
    fade_step = music_fade_step;

  //enable_interrupts();
}

void sound_set_music_gain(float vol) {
  music_gain = vol;
}

void sound_set_listener_pos(float x, float y) {
  //disable_interrupts();
  listener_x = x;
  listener_y = y;
  //enable_interrupts();
}

static int find_free_channel(int priority, int *ch) {
  const int start_ch = first_fx_channel();
  if (start_ch >= NUM_CHANNELS)
    return -1;

  int channel;
  if (ch) {
    channel = *ch;
    if (channel < start_ch)
      channel = -1;
  } else {
    channel = -1;
  }

  if (channel < 0) {
    channel = -1;
    for (int search = start_ch; search < NUM_CHANNELS; search++) {
      if (!mixer_ch_playing(search)) {
        channel = search;
        break;
      }
    }
    if (channel == -1) {
      for (int search = start_ch; search < NUM_CHANNELS; search++) {
        if (current_sfxs[search].priority < priority) {
          channel = search;
          break;
        }
      }
      if (channel == -1) {
        channel = last_sfx_channel - 1;
        if (channel < start_ch)
          channel = NUM_CHANNELS - 1;
      }
    }
  }

  return channel;
}

void sound_play_fx(uint16_t id, float x, float y, int priority, int *ch) {
  assertf(id < NUM_SFX, "invalid sfx id %d", id);

  //disable_interrupts();

  float att = distance_atttenuation(x, y);
  if (att <= 0.0)
    return;

  int channel = find_free_channel(priority, ch);
  if (channel < 0)
    return;

  stop_sfx(channel);

  current_sfxs[channel].sound = id;
  current_sfxs[channel].pos = 0;
  current_sfxs[channel].x = x;
  current_sfxs[channel].y = y;
  current_sfxs[channel].priority = priority;
  current_sfxs[channel].ptr = NULL;
  last_sfx_channel = channel;

  wav64_play(&sfxs[id], channel);

  if (ch) {
    *ch = channel;
    if (!sfxs[id].wave.loop_len) {
      current_sfxs[channel].ptr = ch;
      mixer_add_event(sfxs[id].wave.len, unset_channel_pointer, ch);
    }
  }

  //enable_interrupts();
}

void sound_play_fx_global(uint16_t id, int priority, int *ch) {
  sound_play_fx(id, FLT_MAX, FLT_MAX, priority, ch);
}

void sound_update_position(int ch, float x, float y) {
  if (ch == -1)
    return;

  current_sfxs[ch].x = x;
  current_sfxs[ch].y = y;

  //disable_interrupts();

  //enable_interrupts();
}

void sound_release_channel(int *ptr) {
  //disable_interrupts();

  if (*ptr != -1) {
    unset_channel_pointer(ptr);
    mixer_remove_event(unset_channel_pointer, ptr);
  }

  //enable_interrupts();
}

void sound_pause_fx(void) {
  //disable_interrupts();
  FOREACH_SFX_CHAN(channel) {
    if (mixer_ch_playing(channel)) {
      current_sfxs[channel].pos = mixer_ch_get_pos(channel);
    } else {
      current_sfxs[channel].sound = SFX_NONE;
    }
  }
  //enable_interrupts();
}

void sound_resume_fx(void) {
  //disable_interrupts();
  FOREACH_SFX_CHAN(channel) {
    if (current_sfxs[channel].sound != SFX_NONE) {
      wav64_play(&sfxs[current_sfxs[channel].sound], channel);
      mixer_ch_set_vol(channel, current_sfxs[channel].lvol, current_sfxs[channel].rvol);
      mixer_ch_set_pos(channel, current_sfxs[channel].pos);
      current_sfxs[channel].sound = SFX_NONE;
    }
  }
  //enable_interrupts();
}

void sound_init(void) {
  xmplayer.ctx = NULL;
  for (int i = SFX_NONE + 1; i < NUM_SFX; i++)
    wav64_open(&sfxs[i], sfx_paths[i]);
  audio_init(AUDIO_FREQ, 4);
  mixer_init(NUM_CHANNELS);
  for (size_t i = 0; i < NUM_CHANNELS; i++)
    wav64_play(&sfxs[SFX_NONE], i);
  /*
  audio_set_buffer_callback(sound_cb);
  while (!audio_can_write()) {}
  short *buf = audio_write_begin();
  memset(buf, 0, audio_get_buffer_length() * sizeof(short));
  audio_write_end();
  */
}

