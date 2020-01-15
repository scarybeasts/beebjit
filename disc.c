#include "disc.h"

#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_disc_bytes_per_track = 3125,
  k_disc_tracks_per_disc = 80,
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
};

struct disc_track {
  uint8_t data[k_disc_bytes_per_track];
  uint8_t clocks[k_disc_bytes_per_track];
};

struct disc_side {
  struct disc_track tracks[k_disc_tracks_per_disc];
};

struct disc_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_byte_callback)(void*, uint8_t, uint8_t, int);
  void* p_byte_callback_object;

  struct disc_side lower_side;
  struct disc_side upper_side;

  int is_side_upper;
  uint32_t track;
  uint32_t byte_position;
};

static void
disc_timer_callback(struct disc_struct* p_disc) {
  struct disc_side* p_side;
  struct disc_track* p_track;
  uint8_t data_byte;
  uint8_t clocks_byte;
  int is_index;

  if (p_disc->is_side_upper) {
    p_side = &p_disc->upper_side;
  } else {
    p_side = &p_disc->lower_side;
  }

  p_track = &p_side->tracks[p_disc->track];
  data_byte = p_track->data[p_disc->byte_position];
  clocks_byte = p_track->clocks[p_disc->byte_position];
  is_index = (p_disc->byte_position < k_disc_index_bytes);

  (void) timing_set_timer_value(p_disc->p_timing,
                                p_disc->timer_id,
                                k_disc_ticks_per_byte);

  p_disc->p_byte_callback(p_disc->p_byte_callback_object,
                          data_byte,
                          clocks_byte,
                          is_index);

  p_disc->byte_position++;
  if (p_disc->byte_position == k_disc_bytes_per_track) {
    p_disc->byte_position = 0;
  }
}

struct disc_struct*
disc_create(struct timing_struct* p_timing,
            void (*p_byte_callback)(void* p,
                                    uint8_t data,
                                    uint8_t clock,
                                    int index),
            void* p_byte_callback_object) {
  struct disc_struct* p_disc = malloc(sizeof(struct disc_struct));
  if (p_disc == NULL) {
    errx(1, "cannot allocate disc_struct");
  }

  (void) memset(p_disc, '\0', sizeof(struct disc_struct));

  p_disc->p_timing = p_timing;
  p_disc->p_byte_callback = p_byte_callback;
  p_disc->p_byte_callback_object = p_byte_callback_object;

  p_disc->timer_id = timing_register_timer(p_timing,
                                           disc_timer_callback,
                                           p_disc);

  return p_disc;
}

void
disc_destroy(struct disc_struct* p_disc) {
  assert(!timing_timer_is_running(p_disc->p_timing, p_disc->timer_id));
  free(p_disc);
}

void
disc_load(struct disc_struct* p_disc, const char* p_filename) {
  (void) p_disc;
  (void) p_filename;
}

void
disc_start_spinning(struct disc_struct* p_disc) {
  (void) timing_start_timer_with_value(p_disc->p_timing,
                                       p_disc->timer_id,
                                       k_disc_ticks_per_byte);
}

void
disc_stop_spinning(struct disc_struct* p_disc) {
  (void) timing_stop_timer(p_disc->p_timing, p_disc->timer_id);
}

void
disc_select_side(struct disc_struct* p_disc, int side) {
  p_disc->is_side_upper = side;
}

void
disc_select_track(struct disc_struct* p_disc, uint32_t track) {
  if (track >= k_disc_tracks_per_disc) {
    track = (k_disc_tracks_per_disc - 1);
  }
  p_disc->track = track;
}
