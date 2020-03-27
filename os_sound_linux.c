#include "os_sound.h"

#include <alsa/asoundlib.h>

static const char* k_os_sound_default_device = "default";
/* NOTE: a bit aggressive but it seems to work on my old laptop. */
static uint32_t k_os_sound_default_buffer_size = 512;

struct os_sound_struct {
  char* p_device_name;
  uint32_t sample_rate;
  uint32_t buffer_size;
  uint32_t num_periods;
  uint32_t period_size;
  snd_pcm_t* playback_handle;
};

uint32_t
os_sound_get_default_buffer_size(void) {
  return k_os_sound_default_buffer_size;
}

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size,
                uint32_t num_periods) {
  struct os_sound_struct* p_driver =
      util_mallocz(sizeof(struct os_sound_struct));

  if (p_device_name == NULL) {
    p_driver->p_device_name = strdup(k_os_sound_default_device);
  } else {
    p_driver->p_device_name = strdup(p_device_name);
  }

  p_driver->sample_rate = sample_rate;
  p_driver->buffer_size = buffer_size;
  p_driver->num_periods = num_periods;

  return p_driver;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  if (p_driver->playback_handle != NULL) {
    int ret = snd_pcm_close(p_driver->playback_handle);
    if (ret != 0) {
      errx(1, "snd_pcm_close failed");
    }
  }

  free(p_driver->p_device_name);

  util_free(p_driver);
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  int ret;
  snd_pcm_t* playback_handle;
  snd_pcm_hw_params_t* hw_params;
  unsigned int tmp_uint;
  unsigned int periods;
  snd_pcm_uframes_t tmp_uframes_t;
  snd_pcm_uframes_t period_size;

  unsigned int rate = p_driver->sample_rate;
  unsigned int num_periods = p_driver->num_periods;
  unsigned int rate_ret = rate;

  ret = snd_pcm_open(&playback_handle,
                     p_driver->p_device_name,
                     SND_PCM_STREAM_PLAYBACK,
                     0);
  if (ret != 0) {
    fprintf(stderr, "snd_pcm_open failed\n");
    return -1;
  }

  p_driver->playback_handle = playback_handle;

  /* Blocking is default but be explicit. */
  ret = snd_pcm_nonblock(playback_handle, 0);
  if (ret < 0) {
    errx(1, "snd_pcm_nonblock failed");
  }

  snd_pcm_hw_params_alloca(&hw_params);

  ret = snd_pcm_hw_params_any(p_driver->playback_handle, hw_params);
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
  /* Buffer size is in frames, not bytes. */
  ret = snd_pcm_hw_params_set_buffer_size(playback_handle,
                                          hw_params,
                                          p_driver->buffer_size);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_buffer_size failed");
  }
  ret = snd_pcm_hw_params_set_periods(playback_handle,
                                      hw_params,
                                      num_periods,
                                      0);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_set_periods failed");
  }
  ret = snd_pcm_hw_params(playback_handle, hw_params);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params failed");
  }

  ret = snd_pcm_hw_params_get_channels(hw_params, &tmp_uint);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_channels failed");
  }
  if (tmp_uint != 1) {
    errx(1, "channels is not 1");
  }
  ret = snd_pcm_hw_params_get_rate(hw_params, &tmp_uint, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_rate failed");
  }
  if (tmp_uint != p_driver->sample_rate) {
    errx(1, "sample rate is not %u", p_driver->sample_rate);
  }
  ret = snd_pcm_hw_params_get_buffer_size(hw_params, &tmp_uframes_t);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_buffer_size failed");
  }
  if (tmp_uframes_t != p_driver->buffer_size) {
    errx(1, "buffer size is not %u", p_driver->buffer_size);
  }
  ret = snd_pcm_hw_params_get_periods(hw_params, &periods, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_periods failed");
  }
  if (periods != num_periods) {
    errx(1, "periods is not %d", num_periods);
  }
  ret = snd_pcm_hw_params_get_period_size(hw_params, &period_size, NULL);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_period_size failed");
  }
  if ((period_size * num_periods) != p_driver->buffer_size) {
    errx(1, "unexpected period size %zu", period_size);
  }

  p_driver->period_size = period_size;

  printf("Sound device: %s, rate %d, buffer %d, periods %d, period size %d\n",
         snd_pcm_name(playback_handle),
         (int) p_driver->sample_rate,
         (int) p_driver->buffer_size,
         (int) periods,
         (int) period_size);

  ret = snd_pcm_prepare(playback_handle);
  if (ret != 0) {
    fprintf(stderr, "snd_pcm_prepare failed\n");
    return -1;
  }

  return 0;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  return p_driver->sample_rate;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  return p_driver->buffer_size;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  return p_driver->period_size;
}

static void
os_sound_handle_xrun(struct os_sound_struct* p_driver) {
  snd_pcm_t* playback_handle = p_driver->playback_handle;
  int ret = snd_pcm_prepare(playback_handle);

  if (ret != 0) {
    errx(1, "snd_pcm_prepare failed");
  }
}

uint32_t
os_sound_get_frame_space(struct os_sound_struct* p_driver) {
  snd_pcm_sframes_t num_frames;
  while (1) {
    num_frames = snd_pcm_avail(p_driver->playback_handle);
    if (num_frames < 0) {
      if (num_frames == -EPIPE) {
        os_sound_handle_xrun(p_driver);
        continue;
      } else {
        errx(1, "snd_pcm_avail failed: %ld", num_frames);
      }
    }
    break;
  }

  return num_frames;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  snd_pcm_sframes_t ret;

  snd_pcm_t* playback_handle = p_driver->playback_handle;

  while (1) {
    ret = snd_pcm_writei(playback_handle, p_frames, num_frames);
    if (ret < 0) {
      if (ret == -EPIPE) {
        os_sound_handle_xrun(p_driver);
      } else {
        errx(1, "snd_pcm_writei failed: %ld", ret);
      }
    } else if (ret == num_frames) {
      break;
    } else {
      num_frames -= ret;
    }
  }
}
