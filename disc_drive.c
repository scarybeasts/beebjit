#include "disc_drive.h"

#include "bbc_options.h"
#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "timing.h"
#include "util.h"

#include <assert.h>

enum {
  /* This is 300RPM == 0.2s == 200000us per revolution, 3125 bytes per track,
   * 2 system ticks per us.
   */
  k_disc_system_ticks_per_rev = 400000,
  k_disc_ticks_scale_up = 10000,
  /* My Chinon drive holds the index pulse low for about 4ms. */
  k_disc_index_ticks = (4000 * 2 * k_disc_ticks_scale_up),

  k_disc_max_discs_per_drive = 4,
};

struct disc_drive_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_byte_callback)(void*, uint8_t, uint8_t);
  void* p_byte_callback_object;

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
  uint32_t head_position;
};

static struct disc_struct*
disc_drive_get_disc(struct disc_drive_struct* p_drive) {
  return p_drive->p_discs[p_drive->disc_index];
}

static uint32_t
disc_drive_get_byte_position(struct disc_drive_struct* p_drive,
                             uint32_t* p_track_length,
                             uint32_t* p_per_byte_ticks) {
  uint32_t byte_position;
  uint32_t track_length;
  uint32_t per_byte_ticks;

  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  uint32_t per_rev_ticks = (400000u * 10000);

  track_length = 0;
  if (p_disc != NULL) {
    track_length = disc_get_track_length(p_disc,
                                         p_drive->is_side_upper,
                                         p_drive->track);
  }
  if (track_length == 0) {
    track_length = k_ibm_disc_bytes_per_track;
  }
  assert(track_length <= k_disc_max_bytes_per_track);
  per_byte_ticks = (per_rev_ticks / track_length);
  byte_position = (p_drive->head_position / per_byte_ticks);

  assert(byte_position < track_length);

  if (p_track_length != NULL) {
    *p_track_length = track_length;
  }
  if (p_per_byte_ticks != NULL) {
    *p_per_byte_ticks = per_byte_ticks;
  }

  return byte_position;
}

static void
disc_drive_timer_callback(void* p) {
  uint32_t track_length;
  uint32_t byte_position;
  uint32_t per_byte_ticks;
  uint32_t new_head_position;
  uint64_t this_ticks;
  uint64_t next_ticks;
  uint32_t ticks_delta;

  uint8_t data_byte = 0;
  uint8_t clocks_byte = 0;

  struct disc_drive_struct* p_drive = (struct disc_drive_struct*) p;
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  uint32_t head_position = p_drive->head_position;
  uint32_t track = p_drive->track;
  int is_side_upper = p_drive->is_side_upper;

  byte_position = disc_drive_get_byte_position(p_drive,
                                               &track_length,
                                               &per_byte_ticks);

  if (p_disc != NULL) {
    uint8_t* p_data = disc_get_raw_track_data(p_disc, is_side_upper, track);
    uint8_t* p_clocks = disc_get_raw_track_clocks(p_disc, is_side_upper, track);

    data_byte = p_data[byte_position];
    clocks_byte = p_clocks[byte_position];
  }

  byte_position++;
  if (byte_position == track_length) {
    byte_position = 0;

    if (p_disc != NULL) {
      disc_flush_writes(p_disc);
    }
  }

  new_head_position = (byte_position * per_byte_ticks);
  this_ticks = (head_position / 10000);
  if (new_head_position == 0) {
    next_ticks = 400000;
  } else {
    next_ticks = (new_head_position / 10000);
  }
  ticks_delta = (next_ticks - this_ticks);
  /* This can happen when seeking between tracks of different length. */
  if (ticks_delta == 0) {
    ticks_delta = 1;
  }
  (void) timing_set_timer_value(p_drive->p_timing,
                                p_drive->timer_id,
                                ticks_delta);

  /* If there's an empty patch on the disc surface, the disc drive's head
   * amplifier will typically desperately seek for a signal in the noise,
   * resulting in "weak bits".
   * I've verified this with an oscilloscope on my Chinon F-051MD drive, which
   * has a Motorola MC3470AP head amplifier.
   * We need to return an inconsistent yet deterministic set of weak bits.
   */
  if ((data_byte == 0) && (clocks_byte == 0)) {
    uint64_t ticks = timing_get_total_timer_ticks(p_drive->p_timing);
    data_byte = ticks;
    data_byte ^= (ticks >> 8);
    data_byte ^= (ticks >> 16);
    data_byte ^= (ticks >> 24);
  }

  if (p_drive->p_byte_callback != NULL) {
    p_drive->p_byte_callback(p_drive->p_byte_callback_object,
                             data_byte,
                             clocks_byte);
  }

  p_drive->head_position = new_head_position;
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
  assert(!disc_drive_is_spinning(p_drive));

  p_drive->is_side_upper = 0;
  p_drive->track = 0;
  p_drive->head_position = 0;
  /* NOTE: there's a decision here: does a power-on reset of the beeb change a
   * user "physical" action -- changing the disc in the drive in this case.
   * We decide it does. The disc in the drive is reset to the first in the
   * cycle. This decision is so that a power-on reset can be used as a basis to
   * replay state.
   */
  p_drive->disc_index = 0;
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

void
disc_drive_cycle_disc(struct disc_drive_struct* p_drive) {
  /* NOTE: the instantaneous nature of this change may need revising. A real
   * system will see some sequence of drive not ready / drive empty states!
   */
  struct disc_struct* p_disc;
  const char* p_file_name;
  uint32_t disc_index = p_drive->disc_index;

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

  log_do_log(k_log_disc, k_log_info, "disc file now: %s", p_file_name);
}

void
disc_drive_set_byte_callback(struct disc_drive_struct* p_drive,
                             void (*p_byte_callback)(void* p,
                                                     uint8_t data,
                                                     uint8_t clock),
                             void* p_byte_callback_object) {
  p_drive->p_byte_callback = p_byte_callback;
  p_drive->p_byte_callback_object = p_byte_callback_object;
}

uint32_t
disc_drive_get_track(struct disc_drive_struct* p_drive) {
  return p_drive->track;
}

int
disc_drive_is_index_pulse(struct disc_drive_struct* p_drive) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);

  if (p_disc == NULL) {
    /* With no disc loaded, a drive typically asserts INDEX all the time. */
    return 1;
  }

  /* EMU: the 8271 datasheet says that the index pulse must be held for over
   * 0.5us.
   */
  if (p_drive->head_position < k_disc_index_ticks) {
    return 1;
  }
  return 0;
}

uint32_t
disc_drive_get_head_position(struct disc_drive_struct* p_drive) {
  /* This is currently used for logging. Scale to 0-3124. */
  uint32_t ret = (p_drive->head_position / k_disc_ticks_scale_up);
  ret *= k_ibm_disc_bytes_per_track;
  ret /= k_disc_system_ticks_per_rev;
  return ret;
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
  disc_drive_check_track_needs_write(p_drive);

  p_drive->is_side_upper = side;
}

void
disc_drive_select_track(struct disc_drive_struct* p_drive, int32_t track) {
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
disc_drive_write_byte(struct disc_drive_struct* p_drive,
                      uint8_t data,
                      uint8_t clocks) {
  uint32_t byte_position;

  /* All drives I've seen have a write protect failsafe on the drive itself. */
  if (disc_drive_is_write_protect(p_drive)) {
    return;
  }

  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);

  if (p_disc == NULL) {
    return;
  }

  byte_position = disc_drive_get_byte_position(p_drive, NULL, NULL);
  disc_write_byte(p_disc,
                  p_drive->is_side_upper,
                  p_drive->track,
                  byte_position,
                  data,
                  clocks);
}
