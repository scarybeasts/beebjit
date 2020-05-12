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
   * Or if you like, exactly 64us / 128 ticks.
   * We can get away without floating point here because it's exact. If we had
   * to support different rotational speeds or stretched / compressed disc
   * signal, we'd need to crack out the floating point.
   */
  k_disc_ticks_per_byte = (200000 / 3125 * 2),
  /* My Chinon drive holds the index pulse low for about 4ms, which is 62 bytes
   * worth of rotation.
   */
  k_disc_index_bytes = 62,

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
  int is_side_upper;
  /* Physically always 80 tracks even if we're in 40 track mode. 40 track mode
   * essentially double steps.
   */
  uint32_t track;
  uint32_t byte_position;
  uint32_t disc_index;
};

static struct disc_struct*
disc_drive_get_disc(struct disc_drive_struct* p_drive) {
  return p_drive->p_discs[p_drive->disc_index];
}

static void
disc_drive_timer_callback(void* p) {
  uint8_t data_byte = 0;
  uint8_t clocks_byte = 0;

  struct disc_drive_struct* p_drive = (struct disc_drive_struct*) p;
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
  uint32_t byte_position = p_drive->byte_position;

  if (p_disc != NULL) {
    int is_side_upper = p_drive->is_side_upper;
    uint32_t track = p_drive->track;
    uint8_t* p_data = disc_get_raw_track_data(p_disc, is_side_upper, track);
    uint8_t* p_clocks = disc_get_raw_track_clocks(p_disc, is_side_upper, track);

    if (byte_position == 0) {
      disc_flush_writes(p_disc);
    }

    data_byte = p_data[byte_position];
    clocks_byte = p_clocks[byte_position];
  }

  (void) timing_set_timer_value(p_drive->p_timing,
                                p_drive->timer_id,
                                k_disc_ticks_per_byte);

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

  assert(byte_position < k_ibm_disc_bytes_per_track);
  byte_position++;
  if (byte_position == k_ibm_disc_bytes_per_track) {
    byte_position = 0;
  }
  p_drive->byte_position = byte_position;
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
  p_drive->byte_position = 0;
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
  if (p_drive->byte_position < k_disc_index_bytes) {
    return 1;
  }
  return 0;
}

uint32_t
disc_drive_get_head_position(struct disc_drive_struct* p_drive) {
  return p_drive->byte_position;
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
  (void) timing_start_timer_with_value(p_drive->p_timing,
                                       p_drive->timer_id,
                                       k_disc_ticks_per_byte);
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
disc_drive_select_track(struct disc_drive_struct* p_drive, uint32_t track) {
  disc_drive_check_track_needs_write(p_drive);

  if (track >= k_ibm_disc_tracks_per_disc) {
    track = (k_ibm_disc_tracks_per_disc - 1);
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
  if (new_track < 0) {
    new_track = 0;
  }
  disc_drive_select_track(p_drive, new_track);
}

void
disc_drive_write_byte(struct disc_drive_struct* p_drive,
                      uint8_t data,
                      uint8_t clocks) {
  struct disc_struct* p_disc = disc_drive_get_disc(p_drive);

  if (p_disc == NULL) {
    return;
  }

  disc_write_byte(p_disc,
                  p_drive->is_side_upper,
                  p_drive->track,
                  p_drive->byte_position,
                  data,
                  clocks);
}
