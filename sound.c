#include "sound.h"

#include "bbc_options.h"
#include "util.h"

#include <err.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

enum {
  /* 0-2 square wave tone channels, 3 noise channel. */
  k_sound_num_channels = 4,
};

struct sound_struct {
  /* Configuration. */
  int enabled;
  size_t sample_rate;
  size_t buffer_size;
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
  uint16_t noise_rng;

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
  uint16_t* p_counters = &p_sound->counter[0];
  char* p_outputs = &p_sound->output[0];
  /* These are written by another thread. */
  volatile short* p_volumes = &p_sound->volume[0];
  volatile uint16_t* p_periods = &p_sound->period[0];
  volatile uint16_t* p_noise_rng = &p_sound->noise_rng;
  volatile int* p_noise_type = &p_sound->noise_type;

  for (i = 0; i < sn_frames_per_fill; ++i) {
    short sample = 0;
    for (channel = 0; channel < 4; ++channel) {
      /* Tick the sn76489 clock and see if any timers expire. Flip the flip
       * flops if they do.
       */
      short sample_component = p_volumes[channel];
      uint16_t counter = p_counters[channel];
      char output = p_outputs[channel];
      uint16_t noise_rng = *p_noise_rng;
      int is_noise = 0;
      if (channel == 3) {
        is_noise = 1;
      }

      counter = ((counter - 1) & 0x3ff);
      if (counter == 0) {
        counter = p_periods[channel];
        output = -output;
        p_outputs[channel] = output;

        /* NOTE: we do this like jsbeeb: we only update the random number
         * every two counter expiries, and we have the period values half what
         * they really are. This might mirror the real silicon? It avoids
         * needing more than 10 bits to store the period.
         */
        if (is_noise && (output == 1)) {
          if (*p_noise_type == 0) {
            noise_rng >>= 1;
            if (noise_rng == 0) {
              noise_rng = (1 << 14);
            }
          } else {
            int bit = ((noise_rng & 1) ^ ((noise_rng & 2) >> 1));
            noise_rng = ((noise_rng >> 1) | (bit << 14));
          }
          *p_noise_rng = noise_rng;
        }
      }

      if (is_noise) {
        output = 1;
        if (!(noise_rng & 1)) {
          output = -1;
        }
      }

      p_counters[channel] = counter;

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

  /* Generate the 250kHz signal from the sn76489. */
  sound_fill_sn76489_buffer(p_sound);

  /* Downsample it to host device rate via simple nearest integer index
   * selection.
   */
  for (i = 0; i < host_frames_per_fill; ++i) {
    p_host_frames[i] = p_sn_frames[(size_t) round(resample_index)];
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
  /* Buffer size is in frames, not bytes. */
  ret = snd_pcm_hw_params_set_buffer_size(playback_handle,
                                          hw_params,
                                          p_sound->buffer_size);
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
    errx(1, "sample rate is not %zu", p_sound->sample_rate);
  }
  ret = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
  if (ret != 0) {
    errx(1, "snd_pcm_hw_params_get_buffer_size failed");
  }
  if (buffer_size != p_sound->buffer_size) {
    errx(1, "buffer size is not %zu", p_sound->buffer_size);
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
  printf("Sound device: %s, rate %d, buffer %d, periods %d, period size %d\n",
         snd_pcm_name(playback_handle),
         (int) p_sound->sample_rate,
         (int) p_sound->buffer_size,
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
sound_create(struct bbc_options* p_options) {
  size_t i;
  double volume;
  int option;
  short max_volume;

  const char* p_opt_flags = p_options->p_opt_flags;
  struct sound_struct* p_sound = malloc(sizeof(struct sound_struct));

  if (p_sound == NULL) {
    errx(1, "couldn't allocate sound_struct");
  }
  (void) memset(p_sound, '\0', sizeof(struct sound_struct));

  p_sound->enabled = !util_has_option(p_opt_flags, "sound:off");

  p_sound->sample_rate = 44100;
  if (util_get_int_option(&option, p_opt_flags, "sound:rate=")) {
    p_sound->sample_rate = option;
  }
  p_sound->buffer_size = 512;
  /* This check makes a sample rate of 96kHz work reasonably by upping the
   * default buffer size. At 512 samples per callback @ 96kHz, it's hard for
   * the audio system to keep up.
   */
  if (p_sound->sample_rate > 50000) {
    p_sound->buffer_size = 1024;
  }
  if (util_get_int_option(&option, p_opt_flags, "sound:buffer=")) {
    p_sound->buffer_size = option;
  }
  p_sound->host_frames_per_fill = (p_sound->buffer_size / 4);
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

  max_volume = p_sound->volumes[0xf];

  /* EMU: initial sn76489 state and behavior is something no two sources seem
   * to agree on. It doesn't matter a huge amount for BBC emulation because
   * MOS sets the sound channels up on boot. But the intial BBC power-on
   * noise does arise from power-on sn76489 state.
   * I'm choosing a strategy that sets up the registers as if they're all zero
   * initialized. This leads to max volume, lowest tone in all channels, and
   * the noise channel is periodic.
   */
  /* EMU: note that there are various sn76489 references that cite that chips
   * seem to start with random register values, e.g.:
   * http://www.smspower.org/Development/SN76489
   */
  for (i = 0; i < 4; ++i) {
    /* NOTE: b-em uses volume of 8, mid-way volume. */
    p_sound->volume[i] = max_volume;
    /* NOTE: b-em == 0x3ff, b2 == 0x3ff, jsbeeb == 0 -> 0x3ff, MAME == 0 -> 0.
     * I'm willing to bet jsbeeb is closest but still wrong. jsbeeb flips the
     * output signal to positive immediately as it traverses -1.
     * beebjit is 0 -> 0x3ff, via direct integer underflow, with no output
     * signal flip. This means our first waveform will start negative, sort of
     * matching MAME which notes the sn76489 has "inverted" output.
     */
    p_sound->period[i] = 0;
    /* NOTE: b-em randomizes these counters, maybe to get a phase effect? */
    p_sound->counter[i] = 0;
    p_sound->output[i] = -1;
  }

  /* EMU NOTE: if we zero initialize noise_frequency, this implies a period of
   * 0x10 on the noise channel.
   * The original BBC startup noise does sound like a more complicated tone
   * than just square waves so maybe that is correct:
   * http://www.8bs.com/sounds/bbc.wav
   * I'm deviating from my "zero intiialization" policy here to select a
   * noise frequency register value of 2, which is period 0x40, which sounds
   * closer to the BBC boot sound we all love!
   */
  p_sound->noise_frequency = 2;
  p_sound->period[3] = 0x40;
  p_sound->noise_type = 0;
  p_sound->last_channel = 0;
  /* NOTE: MAME, b-em, b2 initialize here to 0x4000. */
  p_sound->noise_rng = 0;

  p_sound->write_status = 0;

  return p_sound;
}

void
sound_destroy(struct sound_struct* p_sound) {
  if (p_sound->thread_running) {
    int ret;
    assert(p_sound->enabled);
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
  int ret;

  if (!p_sound->enabled) {
    return;
  }

  assert(!p_sound->thread_running);
  ret = pthread_create(&p_sound->sound_thread,
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

  int new_period = -1;
  int old_write_status = p_sound->write_status;
  p_sound->write_status = write;

  if (p_sound->write_status == 0 || old_write_status == 1) {
    return;
  }

  if (data & 0x80) {
    channel = ((data >> 5) & 0x03);
    p_sound->last_channel = channel;
  } else {
    channel = p_sound->last_channel;
  }

  if ((data & 0x90) == 0x90) {
    /* Update volume of channel. */
    unsigned char volume_index = (0x0f - (data & 0x0f));
    p_sound->volume[channel] = p_sound->volumes[volume_index];
  } else if (channel == 3) {
    /* For the noise channel, we only ever update the lower bits. */
    int noise_frequency = (data & 0x03);
    p_sound->noise_frequency = noise_frequency;
    if (noise_frequency == 0) {
      new_period = 0x10;
    } else if (noise_frequency == 1) {
      new_period = 0x20;
    } else if (noise_frequency == 2) {
      new_period = 0x40;
    } else {
      new_period = p_sound->period[2];
    }
    p_sound->noise_type = ((data & 0x04) >> 2);
    p_sound->noise_rng = (1 << 14);
  } else if (data & 0x80) {
    uint16_t old_period = p_sound->period[channel];
    new_period = (data & 0x0f);
    new_period |= (old_period & 0x3f0);
  } else {
    uint16_t old_period = p_sound->period[channel];
    new_period = ((data & 0x3f) << 4);
    new_period |= (old_period & 0x0f);
  }

  if (new_period != -1) {
    p_sound->period[channel] = new_period;
    if (channel == 2 && p_sound->noise_frequency == 3) {
      p_sound->period[3] = new_period;
    }
  }
}

void
sound_set_registers(struct sound_struct* p_sound, unsigned char* p_volumes) {
  size_t i;
  for (i = 0; i < 4; ++i) {
    p_sound->volume[i] = p_sound->volumes[p_volumes[i]];
  }
}
