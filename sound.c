#include "sound.h"

#include <err.h>
#include <math.h>
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
  /* Configuration. */
  size_t sample_rate;
  size_t host_frames_per_fill;

  /* Calculated configuration. */
  double sn_ticks_per_host_tick;
  size_t sn_frames_per_fill;
  short volumes[16];

  /* Internal state. */
  int thread_running;
  int do_exit;
  pthread_t sound_thread;
  size_t cycles;
  short* p_host_frames;
  short* p_sn_frames;
  uint16_t counter[k_sound_num_channels];
  char output[k_sound_num_channels];

  /* Register values / interface from the host. */
  int write_status;
  short volume[k_sound_num_channels];
  uint16_t period[k_sound_num_channels];
  /* 0 - low, 1 - medium, 2 - high, 3 -- use tone generator 1. */
  int noise_frequency;
  /* 1 is white, 0 is periodic. */
  int noise_type;
  int last_channel;
};

static void
sound_fill_sn76489_buffer(struct sound_struct* p_sound) {
  size_t i;
  size_t channel;

  size_t sn_frames_per_fill = p_sound->sn_frames_per_fill;
  short* p_sn_frames = p_sound->p_sn_frames;

  for (i = 0; i < sn_frames_per_fill; ++i) {
    short sample = 0;
    for (channel = 1; channel <= 3; ++channel) {
      /* Tick the sn76489 clock and see if any timers expire. Flip the flip
       * flops if they do.
       */
      short sample_component = p_sound->volume[channel];
      uint16_t counter = p_sound->counter[channel];
      char output = p_sound->output[channel];
      counter--;
      if (counter == 0) {
        counter = p_sound->period[channel];
        output = -output;
        p_sound->output[channel] = output;
      }
      p_sound->counter[channel] = counter;

      if (output == -1) {
        sample_component = -sample_component;
      }
      sample += sample_component;
    }
    p_sn_frames[i] = sample;
  }
}

static void
sound_fill_buffer(struct sound_struct* p_sound) {
  size_t i;

  short* p_host_frames = p_sound->p_host_frames;
  short* p_sn_frames = p_sound->p_sn_frames;
  size_t host_frames_per_fill = p_sound->host_frames_per_fill;
  double resample_step = p_sound->sn_ticks_per_host_tick;
  double resample_index = 0;

  sound_fill_sn76489_buffer(p_sound);

  for (i = 0; i < host_frames_per_fill; ++i) {
    p_host_frames[i] = p_sn_frames[(size_t) resample_index];
    resample_index += resample_step;
  }
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
  unsigned int rate = p_sound->sample_rate;
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
  if (tmp != p_sound->sample_rate) {
    errx(1, "sample rate is not %zu\n", p_sound->sample_rate);
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

  if (period_size != p_sound->host_frames_per_fill) {
    errx(1, "period size %zu not available", p_sound->host_frames_per_fill);
  }

  ret = snd_pcm_prepare(playback_handle);
  if (ret != 0) {
    errx(1, "snd_pcm_prepare failed");
  }

  while (!*p_do_exit) {
    sound_fill_buffer(p_sound);

    ret = snd_pcm_writei(playback_handle, p_sound->p_host_frames, period_size);
    if (ret < 0) {
      if (ret == -EPIPE) {
        printf("sound: xrun\n");
        ret = snd_pcm_prepare(playback_handle);
        if (ret != 0) {
          errx(1, "snd_pcm_prepare failed");
        }
      } else {
        errx(1, "snd_pcm_writei failed: %d", ret);
      }
    } else if ((unsigned int) ret != period_size) {
      errx(1, "snd_pcm_writei short write");
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
  size_t i;
  double volume;

  struct sound_struct* p_sound = malloc(sizeof(struct sound_struct));
  if (p_sound == NULL) {
    errx(1, "couldn't allocate sound_struct");
  }
  (void) memset(p_sound, '\0', sizeof(struct sound_struct));

  p_sound->sample_rate = 44100;
  p_sound->host_frames_per_fill = 128;
  p_sound->sn_ticks_per_host_tick = ((double) 250000.0 /
                                     (double) p_sound->sample_rate);
  p_sound->sn_frames_per_fill = ceil(p_sound->sn_ticks_per_host_tick *
                                     p_sound->host_frames_per_fill);

  p_sound->thread_running = 0;
  p_sound->do_exit = 0;

  p_sound->p_host_frames = malloc(p_sound->host_frames_per_fill * 2);
  if (p_sound->p_host_frames == NULL) {
    errx(1, "couldn't allocate host_frames");
  }
  (void) memset(p_sound->p_host_frames,
                '\0',
                p_sound->host_frames_per_fill * 2);
  p_sound->p_sn_frames = malloc(p_sound->sn_frames_per_fill * 2);
  if (p_sound->p_sn_frames == NULL) {
    errx(1, "couldn't allocate sn_frames");
  }
  (void) memset(p_sound->p_sn_frames, '\0', p_sound->sn_frames_per_fill * 2);

  p_sound->write_status = 0;
  p_sound->noise_frequency = 0;
  p_sound->noise_type = 0;
  p_sound->last_channel = 0;

  for (i = 0; i <= 3; ++i) {
    p_sound->volume[i] = 0;
    p_sound->period[i] = 1;
    p_sound->counter[i] = 1;
    p_sound->output[i] = 1;
  }

  volume = 1.0;
  i = 16;
  do {
    i--;
    if (i == 0) {
      volume = 0.0;
    }
    p_sound->volumes[i] = (32767 * volume) / 4.0;
    volume *= pow(10.0, -0.1);
  } while (i > 0);

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
  free(p_sound->p_host_frames);
  free(p_sound->p_sn_frames);
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
      unsigned char volume_index = (0x0f - (data & 0x0f));
      p_sound->volume[channel] = p_sound->volumes[volume_index];
    } else if (channel == 0) {
      p_sound->noise_frequency = (data & 0x03);
      p_sound->noise_type = ((data & 0x04) >> 2);
    } else {
      uint16_t old_period = p_sound->period[channel];
      p_sound->period[channel] = (data & 0x0f);
      p_sound->period[channel] |= (old_period & 0x3f0);
    }
  }
printf("channel, period, vol: %d %d %d\n", channel, p_sound->period[channel], p_sound->volume[channel]);
}
