#include "tape.h"

#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_tape_ticks_per_byte = 16666,
};

struct tape_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_byte_callback)(void*, uint8_t);
  void* p_byte_callback_object;

  intptr_t file_handle;
};

static void
tape_timer_callback(struct tape_struct* p_tape) {
  (void) p_tape;
}

struct tape_struct*
tape_create(struct timing_struct* p_timing,
            void (*p_byte_callback)(void* p, uint8_t data),
            void* p_byte_callback_object,
            struct bbc_options* p_options) {
  struct tape_struct* p_tape = malloc(sizeof(struct tape_struct));
  if (p_tape == NULL) {
    errx(1, "cannot allocate tape_struct");
  }

  (void) p_options;

  (void) memset(p_tape, '\0', sizeof(struct tape_struct));

  p_tape->p_timing = p_timing;
  p_tape->p_byte_callback = p_byte_callback;
  p_tape->p_byte_callback_object = p_byte_callback_object;

  p_tape->file_handle = k_util_file_no_handle;

  p_tape->timer_id = timing_register_timer(p_timing,
                                           tape_timer_callback,
                                           p_tape);

  return p_tape;
}

void
tape_destroy(struct tape_struct* p_tape) {
  assert(!tape_is_playing(p_tape));
  if (p_tape->file_handle != k_util_file_no_handle) {
    util_file_handle_close(p_tape->file_handle);
  }
  free(p_tape);
}

void
tape_load(struct tape_struct* p_tape, const char* p_filename) {
  (void) p_tape;
  (void) p_filename;
}

int
tape_is_playing(struct tape_struct* p_tape) {
  return timing_timer_is_running(p_tape->p_timing, p_tape->timer_id);
}

void
tape_play(struct tape_struct* p_tape) {
  (void) timing_start_timer_with_value(p_tape->p_timing,
                                       p_tape->timer_id,
                                       k_tape_ticks_per_byte);
}

void
tape_stop(struct tape_struct* p_tape) {
  (void) timing_stop_timer(p_tape->p_timing, p_tape->timer_id);
}
