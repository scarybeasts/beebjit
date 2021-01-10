#include "os_sound.h"

#include "log.h"
#include "util.h"

#include <alsa/asoundlib.h>
#include <pulse/error.h>
#include <pulse/simple.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* k_os_sound_default_device = "default";
/* NOTE: used to be 512, a bit aggressive but it seemed to work on my old
 * laptop mostly. 2048 is still much better than e.g. jsbeeb, b-em, but has
 * some headroom for even slower devices.
 */
static uint32_t k_os_sound_default_buffer_size = 2048;

struct os_sound_struct {
  char* p_device_name;
  uint32_t sample_rate;
  uint32_t buffer_size;
  uint32_t num_periods;
  uint32_t period_size;
  snd_pcm_t* playback_handle;
  pa_simple* p_pa;
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
      util_bail("snd_pcm_close failed");
    }
  }
  if (p_driver->p_pa != NULL) {
    int error = 0;
    int ret = pa_simple_flush(p_driver->p_pa, &error);
    if (ret != 0) {
      util_bail("pa_simple_flush failed: %d", error);
    }
    pa_simple_free(p_driver->p_pa);
  }

  free(p_driver->p_device_name);

  util_free(p_driver);
}

static int
os_sound_init_pulse(struct os_sound_struct* p_driver) {
  pa_sample_spec sample_spec;
  pa_buffer_attr buffer_attrs;
  pa_usec_t latency;
  int error = 0;

  assert(p_driver->p_pa == NULL);

  (void) memset(&sample_spec, '\0', sizeof(sample_spec));
  sample_spec.format = PA_SAMPLE_S16LE;
  sample_spec.rate = p_driver->sample_rate;
  sample_spec.channels = 1;

  p_driver->period_size = (p_driver->buffer_size / p_driver->num_periods);

  (void) memset(&buffer_attrs, '\0', sizeof(buffer_attrs));
  /* All these * 2 are because buffer_size, etc. is stored in samples, but the
   * pulse audio APIs take bytes. We have 2 bytes (16-bits) per sample.
   */
  buffer_attrs.maxlength = (p_driver->buffer_size * 2);
  buffer_attrs.tlength = -1;
  buffer_attrs.prebuf = buffer_attrs.maxlength;
  buffer_attrs.minreq = -1;
  buffer_attrs.fragsize = -1;

  p_driver->p_pa = pa_simple_new(NULL,
                                 "beebjit",
                                 PA_STREAM_PLAYBACK,
                                 NULL,
                                 "playback",
                                 &sample_spec,
                                 NULL,
                                 &buffer_attrs,
                                 &error);
  if (p_driver->p_pa == NULL) {
    log_do_log(k_log_audio, k_log_warning, "can't open pulseaudio: %d", error);
    return -1;
  }

  error = 0;
  latency = pa_simple_get_latency(p_driver->p_pa, &error);
  log_do_log(k_log_audio,
             k_log_info,
             "pulseaudio latency is %d",
             (int) latency);

  return 0;
}

static int
os_sound_init_alsa(struct os_sound_struct* p_driver) {
  int ret;
  snd_pcm_t* playback_handle;
  snd_pcm_hw_params_t* hw_params;
  snd_pcm_sw_params_t* sw_params;
  unsigned int tmp_uint;
  snd_pcm_uframes_t tmp_uframes_t;
  snd_output_t* p_alsa_log;

  ret = snd_pcm_open(&playback_handle,
                     p_driver->p_device_name,
                     SND_PCM_STREAM_PLAYBACK,
                     0);
  if (ret != 0) {
    log_do_log(k_log_audio, k_log_error, "snd_pcm_open failed");
    return -1;
  }
  p_driver->playback_handle = playback_handle;

  ret = snd_output_stdio_attach(&p_alsa_log, stderr, 0);
  if (ret != 0) {
    util_bail("snd_output_stdio_attach failed");
  }

  /* Blocking is default but be explicit. */
  ret = snd_pcm_nonblock(playback_handle, 0);
  if (ret < 0) {
    util_bail("snd_pcm_nonblock failed");
  }

  snd_pcm_hw_params_alloca(&hw_params);

  ret = snd_pcm_hw_params_any(p_driver->playback_handle, hw_params);
  if (ret < 0) {
    util_bail("snd_pcm_hw_params_any failed");
  }
  ret = snd_pcm_hw_params_set_access(playback_handle,
                                     hw_params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_access failed");
  }
  ret = snd_pcm_hw_params_set_format(playback_handle,
                                     hw_params,
                                     SND_PCM_FORMAT_S16_LE);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_format failed");
  }

  /* Sample rate. */
  tmp_uint = p_driver->sample_rate;
  ret = snd_pcm_hw_params_set_rate_near(playback_handle,
                                        hw_params,
                                        &tmp_uint,
                                        NULL);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_rate_near failed");
  }
  ret = snd_pcm_hw_params_get_rate(hw_params, &tmp_uint, NULL);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_get_rate failed");
  }
  if (tmp_uint != p_driver->sample_rate) {
    util_bail("sample rate is not %u", p_driver->sample_rate);
  }

  /* Channels. */
  ret = snd_pcm_hw_params_set_channels(playback_handle, hw_params, 1);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_channels failed");
  }
  ret = snd_pcm_hw_params_get_channels(hw_params, &tmp_uint);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_get_channels failed");
  }
  if (tmp_uint != 1) {
    util_bail("channels is not 1");
  }

  /* Buffer size. It is in frames, not bytes. */
  tmp_uframes_t = p_driver->buffer_size;
  ret = snd_pcm_hw_params_set_buffer_size_near(playback_handle,
                                               hw_params,
                                               &tmp_uframes_t);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_buffer_size_near failed");
  }
  ret = snd_pcm_hw_params_get_buffer_size(hw_params, &tmp_uframes_t);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_get_buffer_size failed");
  }
  if (tmp_uframes_t != p_driver->buffer_size) {
    log_do_log(k_log_audio,
               k_log_info,
               "buffer size %u differs from requested %u",
               (uint32_t) tmp_uframes_t,
               p_driver->buffer_size);
  }
  p_driver->buffer_size = tmp_uframes_t;

  /* Periods. */
  tmp_uint = p_driver->num_periods;
  ret = snd_pcm_hw_params_set_periods_near(playback_handle,
                                           hw_params,
                                           &tmp_uint,
                                           NULL);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_set_periods_near failed");
  }
  ret = snd_pcm_hw_params_get_periods(hw_params, &tmp_uint, NULL);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_get_periods failed");
  }
  if (tmp_uint != p_driver->num_periods) {
    log_do_log(k_log_audio,
               k_log_info,
               "periods %u differs from requested %u",
               tmp_uint,
               p_driver->num_periods);
  }
  p_driver->num_periods = tmp_uint;

  /* Commit hardware parameters. */
  ret = snd_pcm_hw_params(playback_handle, hw_params);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params failed");
  }

  /* Get period size. */
  ret = snd_pcm_hw_params_get_period_size(hw_params, &tmp_uframes_t, NULL);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_get_period_size failed");
  }
  p_driver->period_size = tmp_uframes_t;

  log_do_log(k_log_audio,
             k_log_info,
             "device: %s, rate %d, buffer %d, periods %d, period size %d",
             snd_pcm_name(playback_handle),
             (int) p_driver->sample_rate,
             (int) p_driver->buffer_size,
             (int) p_driver->num_periods,
             (int) p_driver->period_size);

  /* Software parameters. */
  snd_pcm_sw_params_alloca(&sw_params);
  ret = snd_pcm_sw_params_current(playback_handle, sw_params);
  if (ret != 0) {
    util_bail("snd_pcm_hw_params_current failed");
  }

  /* Start playing only when the buffer is full. */
  ret = snd_pcm_sw_params_set_start_threshold(playback_handle,
                                              sw_params,
                                              p_driver->buffer_size);
  if (ret != 0) {
    util_bail("snd_pcm_sw_params_set_start_threshold failed");
  }

  /* Commit software parameters. */
  ret = snd_pcm_sw_params(playback_handle, sw_params);
  if (ret != 0) {
    util_bail("snd_pcm_sw_params failed");
  }

  ret = snd_output_close(p_alsa_log);
  if (ret != 0) {
    util_bail("snd_output_close failed");
  }

  ret = snd_pcm_prepare(playback_handle);
  if (ret != 0) {
    log_do_log(k_log_audio, k_log_warning, "snd_pcm_prepare failed");
    return -1;
  }

  return 0;
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  if (strcmp(p_driver->p_device_name, "@pulse") == 0) {
    return os_sound_init_pulse(p_driver);
  } else {
    return os_sound_init_alsa(p_driver);
  }
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
    util_bail("snd_pcm_prepare failed");
  }
}

static void
os_sound_write_pulse(struct os_sound_struct* p_driver,
                     int16_t* p_frames,
                     uint32_t num_frames) {
  int error;
  int ret;

  if (num_frames == 0) {
    /* Pulse gives invalid argument if you ask it to write 0 bytes. */
    return;
  }

  error = 0;
  ret = pa_simple_write(p_driver->p_pa, p_frames, (num_frames * 2), &error);
  if (ret != 0) {
    log_do_log(k_log_audio,
               k_log_warning,
               "can't write pulseaudio: %d (%s)",
               error,
               pa_strerror(error));
  }
}

static void
os_sound_write_alsa(struct os_sound_struct* p_driver,
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
        util_bail("snd_pcm_writei failed: %ld", ret);
      }
    } else if (ret == num_frames) {
      break;
    } else {
      num_frames -= ret;
    }
  }
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  assert((p_driver->playback_handle != NULL) || (p_driver->p_pa != NULL));

  if (p_driver->p_pa != NULL) {
    os_sound_write_pulse(p_driver, p_frames, num_frames);
  } else {
    os_sound_write_alsa(p_driver, p_frames, num_frames);
  }
}
