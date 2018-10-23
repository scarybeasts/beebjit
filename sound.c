#include "sound.h"

/* TODO: remove. */
#include <stdio.h>

#include <err.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

enum {
  /* 0 noise channel, 1-3 square wave tone channels. */
  k_sound_num_channels = 4,
};

struct sound_struct {
  /* Internal state. */
  int thread_running;
  int do_exit;
  pthread_t sound_thread;
  size_t cycles;

  /* Register values / interface from the host. */
  int write_status;
  unsigned char volume[k_sound_num_channels];
  uint16_t period[k_sound_num_channels];
  /* 0 - low, 1 - medium, 2 - high, 3 -- use tone generator 1. */
  int noise_frequency;
  /* 1 is white, 0 is periodic. */
  int noise_type;
  int last_channel;
};

static void
sound_fill_buffer(struct sound_struct* p_sound,
                  short* p_frames,
                  size_t num_frames) {
  size_t cycles = p_sound->cycles;
  size_t i = 0;
  while (i < num_frames) {
    short value = 10000;
    if ((cycles % 40) >= 20) {
      value = -10000;
    }
    p_frames[i++] = value;
    cycles++;
  }

  p_sound->cycles = cycles;
}

static void*
sound_play_thread(void* p) {
  int ret;
  unsigned int tmp;
  snd_pcm_uframes_t period_size;
  snd_pcm_uframes_t buffer_size;
  unsigned int periods;
  snd_pcm_t* playback_handle;
  snd_pcm_hw_params_t* hw_params;

  struct sound_struct* p_sound = (struct sound_struct*) p;
  volatile int* p_do_exit = &p_sound->do_exit;
  unsigned int rate = 44100;
  unsigned int rate_ret = rate;

  ret = snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (ret != 0) {
    errx(1, "snd_pcm_open failed");
  }

  snd_pcm_hw_params_alloca(&hw_params);

  ret = snd_pcm_hw_params_any(playback_handle, hw_params);
  if (ret < 0) {
    errx(1, "snd_pcm_hw_params_any failed");
  }
  ret = snd_pcm_hw_params_set_access(playback_handle,
                                     hw_params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_access failed");
  }
  ret = snd_pcm_hw_params_set_format(playback_handle,
                                     hw_params,
                                     SND_PCM_FORMAT_S16_LE);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_format failed");
  }
  ret = snd_pcm_hw_params_set_rate_near(playback_handle,
                                        hw_params,
                                        &rate_ret,
                                        0);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_rate_near failed");
  }
  if (rate_ret != rate) {
    errx(1, "snd_pcm_hw_params_set_rate_near, rate %d unavailable", rate);
  }
  ret = snd_pcm_hw_params_set_channels(playback_handle, hw_params, 1);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_channels failed");
  }
  ret = snd_pcm_hw_params_set_buffer_size(playback_handle, hw_params, 512);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_buffer_size failed");
  }
  ret = snd_pcm_hw_params_set_periods(playback_handle, hw_params, 4, 0);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_periods failed");
  }
  ret = snd_pcm_hw_params(playback_handle, hw_params);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params failed");
  }

  ret = snd_pcm_hw_params_get_channels(hw_params, &tmp);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_channels failed");
  }
  if (tmp != 1) {
    errx(1, "channels is not 1");
  }
  ret = snd_pcm_hw_params_get_rate(hw_params, &tmp, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_rate failed");
  }
  if (tmp != 44100) {
    errx(1, "rate is not 44100");
  }
  ret = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_buffer_size failed");
  }
  if (buffer_size != 512) {
    errx(1, "buffer size is not 512");
  }
  ret = snd_pcm_hw_params_get_periods(hw_params, &periods, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_periods failed");
  }
  if (periods != 4) {
    errx(1, "periods is not 4");
  }

  ret = snd_pcm_hw_params_get_period_size(hw_params, &period_size, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_period_size failed");
  }
  printf("Sound device: %s, periods %d, period size %d\n",
         snd_pcm_name(playback_handle),
         (int) periods,
         (int) period_size);

  ret = snd_pcm_prepare(playback_handle);
  if (ret != 0) {
    errx(1, "snd_pcm_prepare failed");
  }

  while (!*p_do_exit) {
    short frames[period_size];

    sound_fill_buffer(p_sound, frames, period_size);

    ret = snd_pcm_writei(playback_handle, frames, period_size);
    if ((unsigned int) ret != period_size) {
      errx(1, "snd_pcm_writei failed");
    }
  }

  ret = snd_pcm_close(playback_handle);
  if (ret != 0) {
    errx(1, "snd_pcm_close failed");
  }

  return NULL;
}

struct sound_struct*
sound_create() {
  struct sound_struct* p_sound = malloc(sizeof(struct sound_struct));
  if (p_sound == NULL) {
    errx(1, "couldn't allocate sound_struct");
  }
  (void) memset(p_sound, '\0', sizeof(struct sound_struct));

  p_sound->thread_running = 0;
  p_sound->do_exit = 0;
  p_sound->write_status = 0;
  p_sound->noise_frequency = 0;
  p_sound->noise_type = 0;
  p_sound->last_channel = 0;

  return p_sound;
}

void
sound_destroy(struct sound_struct* p_sound) {
  if (p_sound->thread_running) {
    int ret;
    assert(!p_sound->do_exit);
    p_sound->do_exit = 1;
    ret = pthread_join(p_sound->sound_thread, NULL);
    if (ret != 0) {
      errx(1, "pthread_join failed");
    }
  }
  free(p_sound);
}

void
sound_start_playing(struct sound_struct* p_sound) {
  int ret = pthread_create(&p_sound->sound_thread,
                           NULL,
                           sound_play_thread,
                           p_sound);
  if (ret != 0) {
    errx(1, "couldn't create sound thread");
  }

  p_sound->thread_running = 1;
}

void
sound_apply_write_bit_and_data(struct sound_struct* p_sound,
                               int write,
                               unsigned char data) {
  int channel;

  int old_write_status = p_sound->write_status;
  p_sound->write_status = write;
  if (p_sound->write_status == 0 || old_write_status == 1) {
    return;
  }

  channel = p_sound->last_channel;

  if (!(data & 0x80)) {
    /* Update of most significant bits of period for the last channel set. */
    uint16_t old_period = p_sound->period[channel];
    p_sound->period[channel] = ((data & 0x3f) << 4);
    p_sound->period[channel] |= (old_period & 0x0f);
  } else {
    int is_volume = !!(data & 0x10);
    /* Set channel plus some form of update. */
    channel = (3 - ((data >> 5) & 0x03));
    p_sound->last_channel = channel;
    if (is_volume) {
      p_sound->volume[channel] = (0x0f - (data & 0x0f));
    } else if (channel == 0) {
      p_sound->noise_frequency = (data & 0x03);
      p_sound->noise_type = ((data & 0x04) >> 2);
    } else {
      uint16_t old_period = p_sound->period[channel];
      p_sound->period[channel] = (data & 0x0f);
      p_sound->period[channel] |= (old_period & 0x0f);
    }
  }
printf("channel, period, vol: %d %d %d\n", channel, p_sound->period[channel], p_sound->volume[channel]);
}
