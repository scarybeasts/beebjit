#include "sound.h"

#include "os_sound.h"
#include "os_thread.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <math.h>
#include <stdlib.h>
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

  /* Calculated configuration. */
  double sn_frames_per_driver_frame;
  uint32_t sn_frames_per_driver_buffer_size;
  int16_t volumes[16];

  /* Internal state. */
  int thread_running;
  int do_exit;
  struct os_thread_struct* p_thread_sound;
  int16_t* p_driver_frames;
  int16_t* p_sn_frames;

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
  int last_channel;

  /* Timing. */
  struct timing_struct* p_timing;
  uint64_t prev_system_ticks;
  uint32_t sn_frames_filled;
};

static void
sound_fill_sn76489_buffer(struct sound_struct* p_sound, uint32_t num_frames) {
  uint32_t i;
  uint8_t channel;

  int16_t* p_sn_frames = p_sound->p_sn_frames;
  uint16_t* p_counters = &p_sound->counter[0];
  uint8_t* p_outputs = &p_sound->output[0];
  uint32_t sn_frames_filled = p_sound->sn_frames_filled;
  /* These are written by another thread. */
  volatile int16_t* p_volumes = &p_sound->volume[0];
  volatile uint16_t* p_periods = &p_sound->period[0];
  volatile uint16_t* p_noise_rng = &p_sound->noise_rng;
  volatile int* p_noise_type = &p_sound->noise_type;

  if ((sn_frames_filled + num_frames) >
      p_sound->sn_frames_per_driver_buffer_size) {
    errx(1, "p_sn_frames overflowed");
  }

  for (i = 0; i < num_frames; ++i) {
    int16_t sample = 0;
    for (channel = 0; channel < 4; ++channel) {
      /* Tick the sn76489 clock and see if any timers expire. Flip the flip
       * flops if they do.
       */
      int16_t sample_component = p_volumes[channel];
      uint16_t counter = p_counters[channel];
      uint8_t output = p_outputs[channel];
      uint16_t noise_rng = *p_noise_rng;
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
        output = (noise_rng & 1);
      }

      p_counters[channel] = counter;

      sample_component *= output;
      sample += sample_component;
    }
    p_sn_frames[sn_frames_filled + i] = sample;
  }

  p_sound->sn_frames_filled += num_frames;
}

static uint32_t
sound_resample_to_driver_buffer(struct sound_struct* p_sound) {
  uint32_t i;

  int16_t* p_driver_frames = p_sound->p_driver_frames;
  int16_t* p_sn_frames = p_sound->p_sn_frames;
  double resample_step = p_sound->sn_frames_per_driver_frame;
  double resample_index = 0;
  uint32_t num_sn_frames = p_sound->sn_frames_filled;
  uint32_t num_driver_frames = (num_sn_frames / resample_step);

  assert(num_sn_frames <= p_sound->sn_frames_per_driver_buffer_size);
  assert(num_driver_frames <= p_sound->driver_buffer_size);

  p_sound->sn_frames_filled = 0;

  /* Downsample it to host device rate via simple nearest integer index
   * selection.
   */
  for (i = 0; i < num_driver_frames; ++i) {
    uint32_t index = round(resample_index);
    assert(index < p_sound->sn_frames_per_driver_buffer_size);
    p_driver_frames[i] = p_sn_frames[index];
    resample_index += resample_step;
  }

  return num_driver_frames;
}

static void*
sound_play_thread(void* p) {
  struct sound_struct* p_sound = (struct sound_struct*) p;
  struct os_sound_struct* p_sound_driver = p_sound->p_driver;
  volatile int* p_do_exit = &p_sound->do_exit;

  while (!*p_do_exit) {
    uint32_t num_driver_frames = os_sound_wait_for_frame_space(p_sound_driver);
    uint32_t num_sn_frames = (num_driver_frames *
                              p_sound->sn_frames_per_driver_frame);
    assert(num_driver_frames <= p_sound->driver_buffer_size);
    assert(num_sn_frames <= p_sound->sn_frames_per_driver_buffer_size);

    sound_fill_sn76489_buffer(p_sound, num_sn_frames);

    /* Note: num_driver_frames may be one less than desired due to rounding
     * down.
     */
    num_driver_frames = sound_resample_to_driver_buffer(p_sound);
    os_sound_write(p_sound_driver,
                   p_sound->p_driver_frames,
                   num_driver_frames);
  }

  return NULL;
}

struct sound_struct*
sound_create(int synchronous, struct timing_struct* p_timing) {
  size_t i;
  double volume;
  int16_t max_volume;

  struct sound_struct* p_sound = malloc(sizeof(struct sound_struct));
  if (p_sound == NULL) {
    errx(1, "couldn't allocate sound_struct");
  }

  (void) memset(p_sound, '\0', sizeof(struct sound_struct));

  p_sound->p_timing = p_timing;
  p_sound->synchronous = synchronous;
  p_sound->thread_running = 0;
  p_sound->do_exit = 0;

  p_sound->prev_system_ticks = 0;
  p_sound->sn_frames_filled = 0;

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
  p_sound->last_channel = 0;
  /* NOTE: MAME, b-em, b2 initialize here to 0x4000. */
  p_sound->noise_rng = 0;

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
    free(p_sound->p_driver_frames);
  }
  if (p_sound->p_sn_frames) {
    free(p_sound->p_sn_frames);
  }
  free(p_sound);
}

void
sound_set_driver(struct sound_struct* p_sound,
                 struct os_sound_struct* p_driver) {
  uint32_t driver_buffer_size;
  uint32_t driver_chunk_size;
  uint32_t sample_rate;

  assert(!sound_is_active(p_sound));
  assert(p_sound->p_driver == NULL);
  assert(!p_sound->thread_running);

  p_sound->p_driver = p_driver;

  sample_rate = os_sound_get_sample_rate(p_driver);
  driver_buffer_size = os_sound_get_buffer_size(p_driver);
  driver_chunk_size = os_sound_get_period_size(p_driver);
  (void) driver_chunk_size;
  assert(driver_chunk_size > 0);

  p_sound->driver_buffer_size = driver_buffer_size;
  /* sn76489 in the BBC ticks at 250kHz (8x divisor on main 2Mhz clock). */
  p_sound->sn_frames_per_driver_frame = ((double) k_sound_clock_rate /
                                         (double) sample_rate);
  p_sound->sn_frames_per_driver_buffer_size =
      ceil(driver_buffer_size * p_sound->sn_frames_per_driver_frame);

  p_sound->p_driver_frames = malloc(driver_buffer_size * sizeof(int16_t));
  if (p_sound->p_driver_frames == NULL) {
    errx(1, "couldn't allocate p_driver_frames");
  }
  (void) memset(p_sound->p_driver_frames,
                '\0',
                (driver_buffer_size * sizeof(int16_t)));
  p_sound->p_sn_frames = malloc(p_sound->sn_frames_per_driver_buffer_size *
                                sizeof(int16_t));
  if (p_sound->p_sn_frames == NULL) {
    errx(1, "couldn't allocate p_sn_frames");
  }
  (void) memset(p_sound->p_sn_frames,
                '\0',
                (p_sound->sn_frames_per_driver_buffer_size * sizeof(int16_t)));
}

void
sound_start_playing(struct sound_struct* p_sound) {
  if (!sound_is_active(p_sound)) {
    return;
  }

  if (p_sound->synchronous) {
    return;
  }

  assert(!p_sound->thread_running);
  p_sound->p_thread_sound = os_thread_create(sound_play_thread, p_sound);
  p_sound->thread_running = 1;
}

int
sound_is_active(struct sound_struct* p_sound) {
  return (p_sound->p_driver != NULL);
}

int
sound_is_synchronous(struct sound_struct* p_sound) {
  return p_sound->synchronous;
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
  /* In fast mode, the ticks delta will be insanely huge and needs capping. */
  if ((sn_frames_filled + delta_sn_ticks) > sn_frames_per_driver_buffer_size) {
    delta_sn_ticks = (sn_frames_per_driver_buffer_size - sn_frames_filled);
  }

  sound_fill_sn76489_buffer(p_sound, delta_sn_ticks);

  p_sound->prev_system_ticks = curr_system_ticks;
}

void
sound_tick(struct sound_struct* p_sound, int blocking) {
  uint32_t num_driver_frames;

  struct os_sound_struct* p_driver = p_sound->p_driver;

  if (!sound_is_active(p_sound)) {
    return;
  }
  if (!p_sound->synchronous) {
    return;
  }

  sound_advance_sn_timing(p_sound);

  num_driver_frames = sound_resample_to_driver_buffer(p_sound);
  if (!blocking) {
    uint32_t driver_frames_space = os_sound_get_frame_space(p_driver);
    if (num_driver_frames > driver_frames_space) {
      num_driver_frames = driver_frames_space;
    }
  }
  os_sound_write(p_driver, p_sound->p_driver_frames, num_driver_frames);
}

void
sound_sn_write(struct sound_struct* p_sound, uint8_t data) {
  int channel;

  int new_period = -1;

  if (sound_is_active(p_sound) && p_sound->synchronous) {
    sound_advance_sn_timing(p_sound);
  }

  if (data & 0x80) {
    channel = ((data >> 5) & 0x03);
    p_sound->last_channel = channel;
  } else {
    channel = p_sound->last_channel;
  }

  if ((data & 0x90) == 0x90) {
    /* Update volume of channel. */
    uint8_t volume_index = (0x0f - (data & 0x0f));
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

  *p_last_channel = p_sound->last_channel;
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

  p_sound->last_channel = last_channel;
  p_sound->noise_type = noise_type;
  p_sound->noise_frequency = noise_frequency;
  p_sound->noise_rng = noise_rng;
}
