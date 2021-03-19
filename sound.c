#include "sound.h"

#include "bbc_options.h"
#include "os_sound.h"
#include "os_thread.h"
#include "os_time.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static const uint32_t k_sound_clock_rate = 250000;
/* BBC master clock 2MHz, 8x divider for 250kHz sn76489 chip. */
static const uint32_t k_sound_clock_divider = 8;

enum {
  /* 0-2 square wave tone channels, 3 noise channel. */
  k_sound_num_channels = 4,
};

struct sound_struct {
  /* Underylying driver. */
  struct os_sound_struct* p_driver;

  /* Configuration. */
  int synchronous;
  uint32_t driver_buffer_size;
  uint32_t driver_period_size;
  uint32_t sample_rate;

  /* Calculated configuration. */
  double sn_frames_per_driver_frame;
  uint32_t sn_frames_per_driver_buffer_size;
  int16_t volumes[16];
  int16_t volume_silence;

  /* Internal state. */
  int thread_running;
  int do_exit;
  struct os_thread_struct* p_thread_sound;
  int16_t* p_driver_frames;
  uint32_t driver_buffer_index;
  int16_t* p_sn_frames;

  /* Resampling. */
  double average_sample_value;
  double average_sample_count;
  double resample_index;
  uint32_t next_sample_start_index;

  /* sn76489 state. */
  uint16_t counter[k_sound_num_channels];
  uint8_t output[k_sound_num_channels];
  uint16_t noise_rng;
  int16_t volume[k_sound_num_channels];
  uint16_t period[k_sound_num_channels];
  /* 0 - low, 1 - medium, 2 - high, 3 -- use tone generator 1. */
  int noise_frequency;
  /* 1 is white, 0 is periodic. */
  int noise_type;
  uint8_t latched_bits;

  /* Timing. */
  struct timing_struct* p_timing;
  struct os_time_sleeper* p_sleeper;
  uint64_t prev_system_ticks;
  uint32_t sn_frames_filled;
  uint64_t last_sound_driver_wakeup_time;
  uint32_t current_sub_period;
  uint32_t sub_period_size;
};

static void
sound_fill_sn76489_buffer(struct sound_struct* p_sound,
                          uint32_t num_frames,
                          int16_t* p_volumes,
                          uint16_t* p_periods,
                          uint16_t noise_rng,
                          int noise_type) {
  uint32_t i;
  uint8_t channel;

  int16_t* p_sn_frames = p_sound->p_sn_frames;
  uint16_t* p_counters = &p_sound->counter[0];
  uint8_t* p_outputs = &p_sound->output[0];
  uint32_t sn_frames_filled = p_sound->sn_frames_filled;
  int16_t volume_silence = p_sound->volume_silence;

  if ((sn_frames_filled + num_frames) >
      p_sound->sn_frames_per_driver_buffer_size) {
    util_bail("p_sn_frames overflowed");
  }

  for (i = 0; i < num_frames; ++i) {
    int16_t sample = 0;
    for (channel = 0; channel < 4; ++channel) {
      /* Tick the sn76489 clock and see if any timers expire. Flip the flip
       * flops if they do.
       */
      int16_t sample_component = volume_silence;
      uint16_t counter = p_counters[channel];
      uint8_t output = p_outputs[channel];
      int is_noise = 0;
      if (channel == 3) {
        is_noise = 1;
      }

      counter = ((counter - 1) & 0x3ff);
      if (counter == 0) {
        counter = p_periods[channel];
        output = !output;
        p_outputs[channel] = output;

        if (is_noise && output) {
          /* NOTE: we do this like jsbeeb: we only update the random number
           * every two counter expiries, and we have the period values half what
           * they really are. This might mirror the real silicon? It avoids
           * needing more than 10 bits to store the period.
           */
          if (noise_type == 0) {
            noise_rng >>= 1;
            if (noise_rng == 0) {
              noise_rng = (1 << 14);
            }
          } else {
            int bit = ((noise_rng & 1) ^ ((noise_rng & 2) >> 1));
            noise_rng = ((noise_rng >> 1) | (bit << 14));
          }
          p_sound->noise_rng = noise_rng;
        }
      }

      if (is_noise) {
        output = (noise_rng & 1);
      }

      p_counters[channel] = counter;

      if (output) {
        sample_component = p_volumes[channel];
      }
      sample += sample_component;
    }
    p_sn_frames[sn_frames_filled + i] = sample;
  }

  p_sound->sn_frames_filled += num_frames;
}

static uint32_t
sound_resample_to_driver_buffer(struct sound_struct* p_sound) {
  uint32_t sn_frames_index;

  int16_t* p_driver_frames = p_sound->p_driver_frames;
  int16_t* p_sn_frames = p_sound->p_sn_frames;
  double resample_step = p_sound->sn_frames_per_driver_frame;
  uint32_t num_sn_frames = p_sound->sn_frames_filled;
  uint32_t driver_buffer_size = p_sound->driver_buffer_size;
  uint32_t driver_buffer_index = p_sound->driver_buffer_index;
  uint32_t num_driver_frames_written = 0;

  /* Carry-over from prevous resample chunk. */
  double average_sample_value = p_sound->average_sample_value;
  double average_sample_count = p_sound->average_sample_count;
  double resample_index = p_sound->resample_index;
  uint32_t next_sample_start_index = p_sound->next_sample_start_index;

  p_sound->sn_frames_filled = 0;

  /* Downsample it to host device rate via average of sample values.
   * Sampled sound playback is very sensitive to the quality of downsampling,
   * so even the splitting of the border sample value into fractions is
   * important.
   */
  for (sn_frames_index = 0;
       (sn_frames_index < num_sn_frames);
       ++sn_frames_index) {
    double this_sample_value = p_sn_frames[sn_frames_index];

    if (sn_frames_index == next_sample_start_index) {
      if (average_sample_count) {
        double dummy;
        double fraction_this = modf(next_sample_start_index, &dummy);
        double fraction_next = (1.0 - fraction_this);
        average_sample_value += (fraction_this * this_sample_value);
        average_sample_count += fraction_this;
        average_sample_value /= average_sample_count;

        if (driver_buffer_index < driver_buffer_size) {
          p_driver_frames[driver_buffer_index] = average_sample_value;
          driver_buffer_index++;
          num_driver_frames_written++;
        }

        average_sample_value = (fraction_next * this_sample_value);
        average_sample_count = fraction_next;
      }

      resample_index += resample_step;
      next_sample_start_index = ceil(resample_index);
    } else {
      average_sample_value += this_sample_value;
      average_sample_count++;
    }
  }

  assert(p_sound->resample_index < resample_step);
  assert((p_sound->driver_buffer_index + num_driver_frames_written) <=
         driver_buffer_size);

  /* Preserve resample state so we don't lose precision as we cross resample
   * chunks.
   */
  p_sound->average_sample_value = average_sample_value;
  p_sound->average_sample_count = average_sample_count;
  p_sound->resample_index = (resample_index - num_sn_frames);
  p_sound->next_sample_start_index = (next_sample_start_index - num_sn_frames);

  p_sound->driver_buffer_index = driver_buffer_index;
  return num_driver_frames_written;
}

static void
sound_direct_write_driver_frames(struct sound_struct* p_sound,
                                 int16_t* p_volumes,
                                 uint16_t* p_periods,
                                 uint16_t noise_rng,
                                 int noise_type,
                                 uint32_t num_frames) {
  uint32_t num_driver_frames;
  int16_t* p_driver_frames = p_sound->p_driver_frames;
  double num_sn_frames;

  num_sn_frames = (num_frames * p_sound->sn_frames_per_driver_frame);

  p_sound->sn_frames_filled = 0;
  sound_fill_sn76489_buffer(p_sound,
                            (uint32_t) num_sn_frames,
                            p_volumes,
                            p_periods,
                            noise_rng,
                            noise_type);

  p_sound->driver_buffer_index = 0;
  num_driver_frames = sound_resample_to_driver_buffer(p_sound);

  /* TODO: the rounding errors causing this need looking into in more detail. */
  if (num_driver_frames < num_frames) {
    int16_t sample = 0;
    if (num_driver_frames > 0) {
      sample = p_driver_frames[num_driver_frames - 1];
    }
    p_driver_frames[num_driver_frames] = sample;
    num_driver_frames++;
  }
  assert(num_driver_frames == num_frames);

  os_sound_write(p_sound->p_driver, p_driver_frames, num_driver_frames);
}

static void*
sound_play_thread(void* p) {
  int16_t volume[4];
  uint16_t period[4];

  struct sound_struct* p_sound = (struct sound_struct*) p;
  uint32_t period_frames = p_sound->driver_period_size;

  /* We read these but the main thread writes them. */
  volatile int* p_do_exit = &p_sound->do_exit;
  volatile int16_t* p_volume = &p_sound->volume[0];
  volatile uint16_t* p_period = &p_sound->period[0];
  volatile uint16_t* p_noise_rng = &p_sound->noise_rng;
  volatile int* p_noise_type = &p_sound->noise_type;

  while (!*p_do_exit) {
    uint32_t i;

    for (i = 0; i < 4; ++i) {
      volume[i] = p_volume[i];
      period[i] = p_period[i];
    }

    sound_direct_write_driver_frames(p_sound,
                                     volume,
                                     period,
                                     *p_noise_rng,
                                     *p_noise_type,
                                     period_frames);
  }

  return NULL;
}

struct sound_struct*
sound_create(int synchronous,
             struct timing_struct* p_timing,
             struct bbc_options* p_options) {
  uint32_t i;
  double volume_scale;
  int positive_silence;

  struct sound_struct* p_sound = util_mallocz(sizeof(struct sound_struct));

  p_sound->p_timing = p_timing;
  p_sound->p_sleeper = os_time_create_sleeper();
  p_sound->synchronous = synchronous;
  p_sound->thread_running = 0;
  p_sound->do_exit = 0;

  p_sound->average_sample_value = 0.0;
  p_sound->average_sample_count = 0.0;
  p_sound->resample_index = 0.0;
  p_sound->next_sample_start_index = 0;

  p_sound->prev_system_ticks = 0;
  p_sound->sn_frames_filled = 0;
  p_sound->driver_buffer_index = 0;

  positive_silence = util_has_option(p_options->p_opt_flags,
                                     "sound:positive-silence");

  volume_scale = 1.0;
  i = 16;
  do {
    int32_t volume;
    i--;
    if (i == 0) {
      volume_scale = 0.0;
    }
    volume = (32767 * volume_scale);
    /* EMU: surprise! The SN76489 outputs positive voltage for silence and no
     * voltage for max volume. The voltage output on the SN76489 sound output
     * pin ranges from ~0 - ~3.6v.
     * Unfortunately, a large constant output to the sound subsystem doesn't
     * seem to mix well with other sounds, so default is zero voltage for
     * silence.
     */
    if (positive_silence) {
      volume = (32767 - volume);
    }
    /* Apportion the full volume range equally across the 4 channels. */
    volume /= 4;
    p_sound->volumes[i] = volume;
    volume_scale *= pow(10.0, -0.1);
  } while (i > 0);

  p_sound->volume_silence = p_sound->volumes[0];

  return p_sound;
}

void
sound_destroy(struct sound_struct* p_sound) {
  if (p_sound->thread_running) {
    assert(p_sound->p_driver != NULL);
    assert(!p_sound->do_exit);
    p_sound->do_exit = 1;
    (void) os_thread_destroy(p_sound->p_thread_sound);
  }
  if (p_sound->p_driver_frames) {
    util_free(p_sound->p_driver_frames);
  }
  if (p_sound->p_sn_frames) {
    util_free(p_sound->p_sn_frames);
  }
  os_time_free_sleeper(p_sound->p_sleeper);
  util_free(p_sound);
}

void
sound_set_driver(struct sound_struct* p_sound,
                 struct os_sound_struct* p_driver) {
  uint32_t driver_buffer_size;
  uint32_t sample_rate;
  uint32_t sub_period_size;
  double sub_period_time_us;

  assert(p_sound->p_driver == NULL);
  assert(!p_sound->thread_running);

  p_sound->p_driver = p_driver;

  sample_rate = os_sound_get_sample_rate(p_driver);
  driver_buffer_size = os_sound_get_buffer_size(p_driver);
  p_sound->sample_rate = sample_rate;
  p_sound->driver_buffer_size = driver_buffer_size;
  p_sound->driver_period_size = os_sound_get_period_size(p_driver);

  /* Calculate the number of time slices to divide a period into, to get around
   * 1.5ms wakeup latency for client timing.
   */
  sub_period_size = (p_sound->driver_period_size * 2);
  do {
    sub_period_size /= 2;
    sub_period_time_us = (sub_period_size / (double) sample_rate * 1000000);
  } while (sub_period_time_us >= 2500);
  p_sound->sub_period_size = sub_period_size;

  /* sn76489 in the BBC ticks at 250kHz (8x divisor on main 2Mhz clock). */
  p_sound->sn_frames_per_driver_frame = ((double) k_sound_clock_rate /
                                         (double) sample_rate);
  p_sound->sn_frames_per_driver_buffer_size =
      floor(driver_buffer_size * p_sound->sn_frames_per_driver_frame);

  p_sound->p_driver_frames = util_mallocz(driver_buffer_size * sizeof(int16_t));
  p_sound->p_sn_frames = util_mallocz(
      p_sound->sn_frames_per_driver_buffer_size * sizeof(int16_t));
}

void
sound_start_playing(struct sound_struct* p_sound) {
  int16_t* p_driver_frames;
  uint32_t driver_buffer_size;
  struct os_sound_struct* p_driver = p_sound->p_driver;

  if (p_driver == NULL) {
    return;
  }

  /* Fill the buffer with silence. */
  p_driver_frames = p_sound->p_driver_frames;
  driver_buffer_size = p_sound->driver_buffer_size;
  (void) memset(p_driver_frames, '\0', (driver_buffer_size * sizeof(int16_t)));
  os_sound_write(p_driver, p_driver_frames, driver_buffer_size);

  if (p_sound->synchronous) {
    return;
  }

  assert(!p_sound->thread_running);
  p_sound->p_thread_sound = os_thread_create(sound_play_thread, p_sound);
  p_sound->thread_running = 1;
}

void
sound_power_on_reset(struct sound_struct* p_sound) {
  uint32_t i;
  int16_t volume_max = p_sound->volumes[0xf];

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
    p_sound->volume[i] = volume_max;
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
    p_sound->output[i] = 0;
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
  p_sound->latched_bits = 0;
  /* NOTE: MAME, b-em, b2 initialize here to 0x4000. */
  p_sound->noise_rng = 0;

  p_sound->prev_system_ticks = 0;
}

int
sound_is_active(struct sound_struct* p_sound) {
  if (p_sound->p_driver == NULL) {
    return 0;
  }
  return 1;
}

int
sound_is_synchronous(struct sound_struct* p_sound) {
  return sound_is_active(p_sound) && p_sound->synchronous;
}

static void
sound_advance_sn_timing(struct sound_struct* p_sound) {
  uint64_t prev_sn_ticks;
  uint64_t curr_sn_ticks;
  uint64_t delta_sn_ticks;

  uint64_t curr_system_ticks =
      timing_get_scaled_total_timer_ticks(p_sound->p_timing);
  uint32_t sn_frames_filled = p_sound->sn_frames_filled;
  uint32_t sn_frames_per_driver_buffer_size =
      p_sound->sn_frames_per_driver_buffer_size;

  prev_sn_ticks = (p_sound->prev_system_ticks / k_sound_clock_divider);
  curr_sn_ticks = (curr_system_ticks / k_sound_clock_divider);
  delta_sn_ticks = (curr_sn_ticks - prev_sn_ticks);
  /* When switching from output disabled (e.g. fast mode) to enabled, the ticks
   * delta will be insanely huge and needs capping.
   */
  if ((sn_frames_filled + delta_sn_ticks) > sn_frames_per_driver_buffer_size) {
    delta_sn_ticks = (sn_frames_per_driver_buffer_size - sn_frames_filled);
  }

  sound_fill_sn76489_buffer(p_sound,
                            delta_sn_ticks,
                            &p_sound->volume[0],
                            &p_sound->period[0],
                            p_sound->noise_rng,
                            p_sound->noise_type);

  p_sound->prev_system_ticks = curr_system_ticks;
}

void
sound_tick(struct sound_struct* p_sound, uint64_t curr_time_us) {
  uint32_t num_full_periods;
  uint32_t num_sub_periods;
  uint32_t num_write_frames;
  uint32_t num_leftover_frames;
  uint32_t driver_buffer_index;
  int16_t* p_driver_frames;

  struct os_sound_struct* p_driver = p_sound->p_driver;
  uint32_t driver_period_size = p_sound->driver_period_size;
  uint32_t sub_period_size = p_sound->sub_period_size;

  assert(sound_is_synchronous(p_sound));

  sound_advance_sn_timing(p_sound);
  (void) sound_resample_to_driver_buffer(p_sound);

  driver_buffer_index = p_sound->driver_buffer_index;
  num_full_periods = (driver_buffer_index / driver_period_size);
  /* Full periods block at the sound driver write. */
  if (num_full_periods > 0) {
    p_driver_frames = p_sound->p_driver_frames;
    num_write_frames = (num_full_periods * driver_period_size);
    os_sound_write(p_driver, p_driver_frames, num_write_frames);

    p_sound->last_sound_driver_wakeup_time = os_time_get_us();

    num_leftover_frames = (driver_buffer_index - num_write_frames);
    (void) memmove(p_driver_frames,
                   (p_driver_frames + num_write_frames),
                   (num_leftover_frames * sizeof(int16_t)));
    p_sound->driver_buffer_index = num_leftover_frames;
    p_sound->current_sub_period = 0;
  }

  /* Sub-periods cause us to sleep for sub-period amount of time. */
  num_sub_periods = (p_sound->driver_buffer_index / sub_period_size);
  if ((num_sub_periods > p_sound->current_sub_period) &&
      (p_sound->last_sound_driver_wakeup_time != 0)) {
    uint64_t sub_period_total_time_us = p_sound->last_sound_driver_wakeup_time;
    double sub_period_time_us = ((double) sub_period_size /
                                 p_sound->sample_rate *
                                 1000000);
    sub_period_total_time_us += (num_sub_periods * sub_period_time_us);
    if (sub_period_total_time_us > curr_time_us) {
      uint64_t delta_us = (sub_period_total_time_us - curr_time_us);
      os_time_sleeper_sleep_us(p_sound->p_sleeper, delta_us);
    }
    p_sound->current_sub_period = num_sub_periods;
  }
}

void
sound_sn_write(struct sound_struct* p_sound, uint8_t value) {
  uint8_t command;
  uint8_t channel;

  int32_t new_period = -1;

  if (sound_is_active(p_sound) && p_sound->synchronous) {
    sound_advance_sn_timing(p_sound);
  }

  if (value & 0x80) {
    p_sound->latched_bits = (value & 0x70);
    command = (value & 0xF0);
  } else {
    command = p_sound->latched_bits;
  }
  channel = ((command >> 5) & 0x03);

  if (command & 0x10) {
    /* Update volume of channel. */
    uint8_t volume_index = (0x0f - (value & 0x0f));
    p_sound->volume[channel] = p_sound->volumes[volume_index];
  } else if (channel == 3) {
    /* For the noise channel, we only ever update the lower bits. */
    int noise_frequency = (value & 0x03);
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
    p_sound->noise_type = ((value & 0x04) >> 2);
    p_sound->noise_rng = (1 << 14);
  } else if (command & 0x80) {
    /* Period low bits. */
    uint16_t old_period = p_sound->period[channel];
    new_period = (value & 0x0f);
    new_period |= (old_period & 0x3f0);
  } else {
    uint16_t old_period = p_sound->period[channel];
    new_period = ((value & 0x3f) << 4);
    new_period |= (old_period & 0x0f);
  }

  if (new_period != -1) {
    p_sound->period[channel] = new_period;
    if ((channel == 2) && (p_sound->noise_frequency == 3)) {
      p_sound->period[3] = new_period;
    }
  }
}

static uint8_t
sound_inverse_volume_lookup(struct sound_struct* p_sound, int16_t volume) {
  size_t i;
  for (i = 0; i < 16; ++i) {
    if (p_sound->volumes[i] == volume) {
      return i;
    }
  }
  assert(0);
  return 0;
}

void
sound_get_state(struct sound_struct* p_sound,
                uint8_t* p_volumes,
                uint16_t* p_periods,
                uint16_t* p_counters,
                uint8_t* p_outputs,
                uint8_t* p_last_channel,
                int* p_noise_type,
                uint8_t* p_noise_frequency,
                uint16_t* p_noise_rng) {
  size_t i;
  for (i = 0; i < 4; ++i) {
    p_volumes[i] = sound_inverse_volume_lookup(p_sound, p_sound->volume[i]);
    p_periods[i] = p_sound->period[i];
    p_counters[i] = p_sound->counter[i];
    p_outputs[i] = p_sound->output[i];
  }

  *p_last_channel = (p_sound->latched_bits >> 5);
  *p_noise_type = p_sound->noise_type;
  *p_noise_frequency = p_sound->noise_frequency;
  *p_noise_rng = p_sound->noise_rng;
}

void
sound_set_state(struct sound_struct* p_sound,
                uint8_t* p_volumes,
                uint16_t* p_periods,
                uint16_t* p_counters,
                uint8_t* p_outputs,
                uint8_t last_channel,
                int noise_type,
                uint8_t noise_frequency,
                uint16_t noise_rng) {
  size_t i;
  for (i = 0; i < 4; ++i) {
    p_sound->volume[i] = p_sound->volumes[p_volumes[i]];
    p_sound->period[i] = p_periods[i];
    p_sound->counter[i] = p_counters[i];
    p_sound->output[i] = p_outputs[i];
  }

  p_sound->latched_bits = (last_channel << 5);
  p_sound->noise_type = noise_type;
  p_sound->noise_frequency = noise_frequency;
  p_sound->noise_rng = noise_rng;
}
