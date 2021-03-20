#include "disc_drive.h"

#include "bbc_options.h"
#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "timing.h"
#include "util.h"

#include <assert.h>

enum {
  /* My Chinon drive holds the index pulse low for about 4ms. */
  k_disc_index_ms = 4,

  k_disc_max_discs_per_drive = 4,

  k_disc_drive_ticks_per_revolution = 400000,
};

struct disc_drive_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_pulses_callback)(void*, uint32_t, uint32_t);
  void* p_pulses_callback_object;
  int is_32us_mode;

  /* Properties of the drive. */
  uint32_t id;
  struct disc_struct* p_discs[k_disc_max_discs_per_drive + 1];
  uint32_t discs_added;
  int is_40_track;

  /* State of the drive. */
  uint32_t disc_index;
  int is_side_upper;
  /* Physically always 80 tracks even if we're in 40 track mode. 40 track mode
   * essentially double steps.
   */
  uint32_t track;
  /* In units where 3125 is a normal track length. */
  uint32_t head_position;
  /* Extra precision for head position, needed for MFM. */
  uint32_t pulse_position;
};

struct disc_struct*
disc_drive_get_disc(struct disc_drive_struct* p_drive) {
  return p_drive->p_discs[p_drive->disc_index];
}

static double
disc_get_fraction_for_position(uint32_t track_length,
                               uint32_t head_position,
                               uint32_t pulse_position) {
  double ret;

  assert(head_position <= track_length);
  if (head_position == track_length) {
    assert(pulse_position == 0);
  } else {
    assert(pulse_position < 32);
  }

  ret = head_position;
  ret += (pulse_position / (double) 32);
  ret = (ret / track_length);

  return ret;
}

static uint32_t
disc_get_time_for_position(uint32_t track_length,
                           uint32_t head_position,
                           uint32_t pulse_position) {
  double fraction = disc_get_fraction_for_position(track_length,
                                                   head_position,
                                                   pulse_position);
  /* 300rpm disc rotation speed, i.e. 5 per second.
   * Could easily make drive speed configurable.
   */
  fraction *= k_disc_drive_ticks_per_revolution;

  return (uint32_t) fraction;
}

static uint32_t
disc_drive_get_track_length(struct disc_drive_struct* p_drive) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  uint32_t track_length = k_ibm_disc_bytes_per_track;

  if (p_disc != NULL) {
    track_length = disc_get_track_length(p_disc,
                                         p_drive->is_side_upper,
                                         p_drive->track);
  }
  return track_length;
}

uint32_t
disc_drive_get_quasi_random_pulses(struct disc_drive_struct* p_drive) {
  uint64_t ticks = timing_get_total_timer_ticks(p_drive->p_timing);
  uint8_t fm_data = (ticks & 0xFF);
  fm_data ^= (ticks >> 8);
  fm_data ^= (ticks >> 16);
  fm_data ^= (ticks >> 24);
  return ibm_disc_format_fm_to_2us_pulses(0xFF, fm_data);
}

static void
disc_drive_timer_callback(void* p) {
  uint32_t track_length;
  uint32_t this_ticks;
  uint32_t next_ticks;
  uint32_t num_pulses;

  uint32_t pulses = 0;

  struct disc_drive_struct* p_drive = (struct disc_drive_struct*) p;
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  uint32_t track = p_drive->track;
  int is_side_upper = p_drive->is_side_upper;
  uint32_t head_position = p_drive->head_position;
  uint32_t pulse_position = p_drive->pulse_position;

  if (p_disc != NULL) {
    pulses = disc_read_pulses(p_disc, is_side_upper, track, head_position);
  }

  assert((pulse_position == 0) || (pulse_position == 16));
  if ((pulse_position == 16) || p_drive->is_32us_mode) {
    num_pulses = 16;
    if (pulse_position == 0) {
      pulses >>= 16;
    } else {
      pulses &= 0xFFFF;
    }
  } else {
    num_pulses = 32;
  }

  /* If there's an empty patch on the disc surface, the disc drive's head
   * amplifier will typically desperately seek for a signal in the noise,
   * resulting in "weak bits".
   * I've verified this with an oscilloscope on my Chinon F-051MD drive, which
   * has a Motorola MC3470AP head amplifier.
   * We need to return an inconsistent yet deterministic set of weak bits.
   */
  if (pulses == 0) {
    pulses = disc_drive_get_quasi_random_pulses(p_drive);
  }

  if (p_drive->p_pulses_callback != NULL) {
    p_drive->p_pulses_callback(p_drive->p_pulses_callback_object,
                               pulses,
                               num_pulses);
  }

  /* Reload in case the callback changed things. */
  head_position = p_drive->head_position;
  pulse_position = p_drive->pulse_position;
  track_length = disc_drive_get_track_length(p_drive);
  assert(head_position < track_length);

  this_ticks = disc_get_time_for_position(track_length,
                                          head_position,
                                          pulse_position);

  /* Advance head position. */
  if (num_pulses == 16) {
    if (pulse_position == 0) {
      pulse_position = 16;
    } else {
      pulse_position = 0;
      head_position++;
    }
  } else {
    head_position++;
  }

  next_ticks = disc_get_time_for_position(track_length,
                                          head_position,
                                          pulse_position);

  if (head_position == track_length) {
    assert(pulse_position == 0);
    head_position = 0;

    if (p_disc != NULL) {
      disc_flush_writes(p_disc);
    }
  }

  p_drive->head_position = head_position;
  p_drive->pulse_position = pulse_position;

  assert(next_ticks > this_ticks);

  (void) timing_set_timer_value(p_drive->p_timing,
                                p_drive->timer_id,
                                (next_ticks - this_ticks));
}

struct disc_drive_struct*
disc_drive_create(uint32_t id,
                  struct timing_struct* p_timing,
                  struct bbc_options* p_options) {
  struct disc_drive_struct* p_drive =
      util_mallocz(sizeof(struct disc_drive_struct));

  p_drive->id = id;
  p_drive->p_timing = p_timing;

  if (id == 0) {
    p_drive->is_40_track = util_has_option(p_options->p_opt_flags,
                                           "disc:drive0-40");
  } else if (id == 1) {
    p_drive->is_40_track = util_has_option(p_options->p_opt_flags,
                                           "disc:drive1-40");
  }

  p_drive->timer_id = timing_register_timer(p_timing,
                                            disc_drive_timer_callback,
                                            p_drive);

  return p_drive;
}

void
disc_drive_destroy(struct disc_drive_struct* p_drive) {
  uint32_t i;

  assert(!disc_drive_is_spinning(p_drive));

  for (i = 0; i < k_disc_max_discs_per_drive; ++i) {
    struct disc_struct* p_disc = p_drive->p_discs[i];
    if (p_disc != NULL) {
      util_free(p_disc);
    }
  }

  util_free(p_drive);
}

void
disc_drive_power_on_reset(struct disc_drive_struct* p_drive) {
  uint32_t i_disc;

  assert(!disc_drive_is_spinning(p_drive));

  p_drive->is_side_upper = 0;
  p_drive->track = 0;
  p_drive->head_position = 0;
  p_drive->pulse_position = 0;
  /* NOTE: there's a decision here: does a power-on reset of the beeb change a
   * user "physical" action -- changing the disc in the drive in this case.
   * We decide it does. The disc in the drive is reset to the first in the
   * cycle. This decision is so that a power-on reset can be used as a basis to
   * replay state.
   */
  p_drive->disc_index = 0;

  for (i_disc = 0; i_disc < p_drive->discs_added; ++i_disc) {
    disc_load(p_drive->p_discs[i_disc]);
  }
}

void
disc_drive_add_disc(struct disc_drive_struct* p_drive,
                    struct disc_struct* p_disc) {
  uint32_t discs_added = p_drive->discs_added;
  if (discs_added == k_disc_max_discs_per_drive) {
    util_bail("disc drive already at max discs");
  }

  p_drive->p_discs[discs_added] = p_disc;
  p_drive->discs_added++;
}

static double
disc_drive_get_position_fraction(struct disc_drive_struct* p_drive) {
  uint32_t track_length = disc_drive_get_track_length(p_drive);
  return disc_get_fraction_for_position(track_length,
                                        p_drive->head_position,
                                        p_drive->pulse_position);
}

static void
disc_drive_set_position_fraction(struct disc_drive_struct* p_drive,
                                 double fraction) {
  uint32_t track_length = disc_drive_get_track_length(p_drive);
  uint32_t new_head_position = (track_length * fraction);
  p_drive->head_position = new_head_position;
  p_drive->pulse_position = 0;
}

void
disc_drive_cycle_disc(struct disc_drive_struct* p_drive) {
  /* NOTE: the instantaneous nature of this change may need revising. A real
   * system will see some sequence of drive not ready / drive empty states!
   */
  struct disc_struct* p_disc;
  const char* p_file_name;
  uint32_t disc_index = p_drive->disc_index;
  double fraction = disc_drive_get_position_fraction(p_drive);

  if (disc_index == p_drive->discs_added) {
    disc_index = 0;
  } else {
    disc_index++;
  }

  p_drive->disc_index = disc_index;
  p_disc = p_drive->p_discs[disc_index];
  if (p_disc == NULL) {
    p_file_name = "<none>";
  } else {
    p_file_name = disc_get_file_name(p_disc);
  }

  log_do_log(k_log_disc,
             k_log_info,
             "drive %d file now: %s",
             p_drive->id,
             p_file_name);

  disc_drive_set_position_fraction(p_drive, fraction);
}

void
disc_drive_set_pulses_callback(struct disc_drive_struct* p_drive,
                               void (*p_pulses_callback)(void* p,
                                                         uint32_t pulses,
                                                         uint32_t count),
                               void* p_pulses_callback_object) {
  p_drive->p_pulses_callback = p_pulses_callback;
  p_drive->p_pulses_callback_object = p_pulses_callback_object;
}

void
disc_drive_set_32us_mode(struct disc_drive_struct* p_drive, int on) {
  p_drive->is_32us_mode = on;
}

uint32_t
disc_drive_get_track(struct disc_drive_struct* p_drive) {
  return p_drive->track;
}

int
disc_drive_is_index_pulse(struct disc_drive_struct* p_drive) {
  uint32_t track_length;
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);

  if (p_disc == NULL) {
    /* With no disc loaded, a drive typically asserts INDEX all the time. */
    return 1;
  }

  /* EMU: the 8271 datasheet says that the index pulse must be held for over
   * 0.5us. Most drives are in the milisecond range.
   */
  track_length = disc_drive_get_track_length(p_drive);
  if (p_drive->head_position <
          (track_length * (k_disc_index_ms / (double) 200))) {
    return 1;
  }
  return 0;
}

uint32_t
disc_drive_get_head_position(struct disc_drive_struct* p_drive) {
  return p_drive->head_position;
}

int
disc_drive_is_write_protect(struct disc_drive_struct* p_drive) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);

  if (p_disc == NULL) {
    /* EMU: a drive will typically return write enabled with no disc in. */
    return 0;
  }

  return disc_is_write_protected(p_disc);
}

int
disc_drive_is_spinning(struct disc_drive_struct* p_drive) {
  return timing_timer_is_running(p_drive->p_timing, p_drive->timer_id);
}

void
disc_drive_start_spinning(struct disc_drive_struct* p_drive) {
  (void) timing_start_timer_with_value(p_drive->p_timing, p_drive->timer_id, 1);
}

static void
disc_drive_check_track_needs_write(struct disc_drive_struct* p_drive) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  if (p_disc != NULL) {
    disc_flush_writes(p_disc);
  }
}

void
disc_drive_stop_spinning(struct disc_drive_struct* p_drive) {
  disc_drive_check_track_needs_write(p_drive);

  (void) timing_stop_timer(p_drive->p_timing, p_drive->timer_id);
}

void
disc_drive_select_side(struct disc_drive_struct* p_drive, int side) {
  double fraction = disc_drive_get_position_fraction(p_drive);
  disc_drive_check_track_needs_write(p_drive);

  p_drive->is_side_upper = side;

  disc_drive_set_position_fraction(p_drive, fraction);
}

void
disc_drive_select_track(struct disc_drive_struct* p_drive, int32_t track) {
  double fraction = disc_drive_get_position_fraction(p_drive);
  disc_drive_check_track_needs_write(p_drive);

  if (track < 0) {
    track = 0;
    log_do_log(k_log_disc, k_log_unusual, "clang! disc head stopper @ track 0");
  } else if (track >= k_ibm_disc_tracks_per_disc) {
    track = (k_ibm_disc_tracks_per_disc - 1);
    log_do_log(k_log_disc,
               k_log_unusual,
               "clang! disc head stopper @ track max");
  }
  p_drive->track = track;

  disc_drive_set_position_fraction(p_drive, fraction);
}

void
disc_drive_seek_track(struct disc_drive_struct* p_drive, int32_t delta) {
  int32_t new_track;

  if (p_drive->is_40_track) {
    delta *= 2;
  }
  new_track = ((int32_t) p_drive->track + delta);
  disc_drive_select_track(p_drive, new_track);
}

void
disc_drive_write_pulses(struct disc_drive_struct* p_drive, uint32_t pulses) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  int is_side_upper = p_drive->is_side_upper;
  uint32_t track = p_drive->track;
  uint32_t head_position = p_drive->head_position;

  if (p_disc == NULL) {
    return;
  }

  /* All drives I've seen have a write protect failsafe on the drive itself. */
  if (disc_drive_is_write_protect(p_drive)) {
    return;
  }

  if (p_drive->is_32us_mode) {
    uint32_t read_pulses = disc_read_pulses(p_disc,
                                            is_side_upper,
                                            track,
                                            head_position);
    assert(pulses <= 0xFFFF);
    if (p_drive->pulse_position == 0) {
      pulses = ((read_pulses & 0x0000FFFF) | (pulses << 16));
    } else {
      pulses = ((read_pulses & 0xFFFF0000) | pulses);
    }
  }
  disc_write_pulses(p_disc, is_side_upper, track, head_position, pulses);
}
