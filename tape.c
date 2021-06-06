#include "tape.h"

#include "bbc_options.h"
#include "log.h"
#include "serial.h"
#include "tape_csw.h"
#include "tape_uef.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

enum {
  k_tape_max_tapes = 4,
  k_tape_system_tick_rate = 2000000,
  k_tape_bit_rate = 1200,
  k_tape_ticks_per_bit = (k_tape_system_tick_rate / k_tape_bit_rate),
  /* Includes a start and stop bit. */
  k_tape_ticks_per_byte = (k_tape_ticks_per_bit * 10),
};

enum {
  k_tape_uef_value_carrier = -1,
  k_tape_uef_value_silence = -2,
};

struct tape_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;
  struct serial_struct* p_serial;
  void (*p_status_callback)(void* p, int carrier, int32_t value);
  void* p_status_callback_object;

  uint32_t tick_rate;

  char* p_tape_file_names[k_tape_max_tapes + 1];
  int32_t* p_tape_buffers[k_tape_max_tapes + 1];
  uint32_t num_tape_values[k_tape_max_tapes + 1];

  uint32_t tapes_added;
  uint32_t tape_index;
  uint64_t tape_buffer_pos;

  int32_t* p_build_buf;
};

static void
tape_timer_callback(struct tape_struct* p_tape) {
  int32_t tape_value;

  uint32_t tape_index = p_tape->tape_index;
  uint64_t tape_buffer_pos = p_tape->tape_buffer_pos;
  uint32_t num_tape_values = p_tape->num_tape_values[tape_index];
  int32_t* p_tape_buffer = p_tape->p_tape_buffers[tape_index];
 
  int carrier = 0;

  (void) timing_set_timer_value(p_tape->p_timing,
                                p_tape->timer_id,
                                p_tape->tick_rate);

  if (p_tape->tape_buffer_pos < num_tape_values) {
    tape_value = p_tape_buffer[tape_buffer_pos];
  } else {
    tape_value = k_tape_uef_value_silence;
  }

  if (tape_value == k_tape_uef_value_carrier) {
    carrier = 1;
  }

  if (p_tape->p_status_callback) {
    p_tape->p_status_callback(p_tape->p_status_callback_object,
                              carrier,
                              tape_value);
  }

  p_tape->tape_buffer_pos++;
}

struct tape_struct*
tape_create(struct timing_struct* p_timing, struct bbc_options* p_options) {
  struct tape_struct* p_tape = util_mallocz(sizeof(struct tape_struct));

  p_tape->p_timing = p_timing;

  p_tape->timer_id = timing_register_timer(p_timing,
                                           tape_timer_callback,
                                           p_tape);

  p_tape->tick_rate = k_tape_ticks_per_byte;
  (void) util_get_u32_option(&p_tape->tick_rate,
                             p_options->p_opt_flags,
                             "tape:tick-rate=");

  p_tape->tapes_added = 0;
  p_tape->tape_index = 0;
  p_tape->tape_buffer_pos = 0;
  p_tape->p_tape_buffers[0] = NULL;

  return p_tape;
}

void
tape_destroy(struct tape_struct* p_tape) {
  uint32_t i;

  assert(!tape_is_playing(p_tape));
  for (i = 0; i < p_tape->tapes_added; ++i) {
    util_free(p_tape->p_tape_file_names[i]);
    util_free(p_tape->p_tape_buffers[i]);
  }
  util_free(p_tape);
}

void
tape_set_status_callback(struct tape_struct* p_tape,
                         void (*p_status_callback)(void* p,
                                                   int carrier,
                                                   int32_t value),
                         void* p_status_callback_object) {
  p_tape->p_status_callback = p_status_callback;
  p_tape->p_status_callback_object = p_status_callback_object;
}

void
tape_power_on_reset(struct tape_struct* p_tape) {
  assert(!tape_is_playing(p_tape));
  tape_rewind(p_tape);
}

void
tape_add_tape(struct tape_struct* p_tape, const char* p_file_name) {
  uint8_t* p_in_file_buf;
  size_t len;
  struct util_file* p_file;
  size_t tape_buffer_size;
  int32_t* p_tape_buffer;

  uint32_t tapes_added = p_tape->tapes_added;

  if (tapes_added == k_tape_max_tapes) {
    util_bail("too many tapes added");
  }

  p_in_file_buf = util_malloc(k_tape_max_file_size);
  p_tape->p_build_buf = util_malloc(k_tape_max_file_size * sizeof(int32_t));

  p_file = util_file_open(p_file_name, 0, 0);

  len = util_file_read(p_file, p_in_file_buf, k_tape_max_file_size);

  util_file_close(p_file);

  p_tape->tape_buffer_pos = 0;
  if (util_is_extension(p_file_name, "csw")) {
    tape_csw_load(p_tape, p_in_file_buf, len);
  } else {
    tape_uef_load(p_tape, p_in_file_buf, len);
  }

  tape_buffer_size = (p_tape->tape_buffer_pos * sizeof(int32_t));
  p_tape_buffer = util_malloc(tape_buffer_size);

  p_tape->p_tape_file_names[tapes_added] = util_strdup(p_file_name);
  p_tape->p_tape_buffers[tapes_added] = p_tape_buffer;
  p_tape->num_tape_values[tapes_added] = p_tape->tape_buffer_pos;

  (void) memcpy(p_tape_buffer, p_tape->p_build_buf, tape_buffer_size);

  /* Always end with an empty slot. */
  p_tape->p_tape_buffers[tapes_added + 1] = NULL;
  p_tape->tapes_added++;

  util_free(p_in_file_buf);
  util_free(p_tape->p_build_buf);
  p_tape->p_build_buf = NULL;
}

int
tape_is_playing(struct tape_struct* p_tape) {
  return timing_timer_is_running(p_tape->p_timing, p_tape->timer_id);
}

void
tape_play(struct tape_struct* p_tape) {
  (void) timing_start_timer_with_value(p_tape->p_timing,
                                       p_tape->timer_id,
                                       p_tape->tick_rate);
}

void
tape_stop(struct tape_struct* p_tape) {
  (void) timing_stop_timer(p_tape->p_timing, p_tape->timer_id);
  if (p_tape->p_status_callback) {
    p_tape->p_status_callback(p_tape->p_status_callback_object, 0, -1);
  }
}

void
tape_cycle_tape(struct tape_struct* p_tape) {
  int32_t* p_tape_buffer;
  char* p_file_name;

  uint32_t tape_index = p_tape->tape_index;

  if (tape_index == p_tape->tapes_added) {
    tape_index = 0;
  } else {
    tape_index++;
  }

  p_tape->tape_index = tape_index;
  p_tape_buffer = p_tape->p_tape_buffers[tape_index];
  if (p_tape_buffer == NULL) {
    p_file_name = "<none>";
  } else {
    p_file_name = p_tape->p_tape_file_names[tape_index];
  }

  log_do_log(k_log_tape, k_log_info, "tape file now: %s", p_file_name);

  tape_rewind(p_tape);
}

void
tape_rewind(struct tape_struct* p_tape) {
  p_tape->tape_buffer_pos = 0;
}

static int
tape_check_build_space(struct tape_struct* p_tape) {
  if (p_tape->tape_buffer_pos < k_tape_max_file_size) {
    return 1;
  }

  log_do_log(k_log_tape, k_log_warning, "tape file too large");

  return 0;
}

void
tape_add_silence_bits(struct tape_struct* p_tape, uint32_t num_bits) {
  uint32_t i_chunks;
  uint32_t num_10bit_chunks = (num_bits / 10);
  if (num_10bit_chunks == 0) {
    num_10bit_chunks = 1;
  }
  for (i_chunks = 0; i_chunks < num_10bit_chunks; ++i_chunks) {
    if (!tape_check_build_space(p_tape)) {
      return;
    }
    p_tape->p_build_buf[p_tape->tape_buffer_pos++] = k_tape_uef_value_silence;
  }
}

void
tape_add_carrier_bits(struct tape_struct* p_tape, uint32_t num_bits) {
  uint32_t i_chunks;
  uint32_t num_10bit_chunks = (num_bits / 10);
  if (num_10bit_chunks == 0) {
    num_10bit_chunks = 1;
  }
  for (i_chunks = 0; i_chunks < num_10bit_chunks; ++i_chunks) {
    if (!tape_check_build_space(p_tape)) {
      return;
    }
    p_tape->p_build_buf[p_tape->tape_buffer_pos++] = k_tape_uef_value_carrier;
  }
}

void
tape_add_byte(struct tape_struct* p_tape, uint8_t byte) {
  if (!tape_check_build_space(p_tape)) {
    return;
  }
  p_tape->p_build_buf[p_tape->tape_buffer_pos++] = byte;
}
