#include "tape.h"

#include "bbc_options.h"
#include "log.h"
#include "serial.h"
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
  k_tape_uef_chunk_origin = 0x0000,
  k_tape_uef_chunk_data = 0x0100,
  k_tape_uef_chunk_defined_format_data = 0x0104,
  k_tape_uef_chunk_carrier_tone = 0x0110,
  k_tape_uef_chunk_carrier_tone_with_dummy_byte = 0x0111,
  k_tape_uef_chunk_gap_int = 0x0112,
  k_tape_uef_chunk_set_baud_rate = 0x0113,
  k_tape_uef_chunk_security_cycles = 0x0114,
  k_tape_uef_chunk_phase_change = 0x0115,
  k_tape_uef_chunk_gap_float = 0x0116,
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

static uint16_t
tape_read_u16(uint8_t* p_in_buf) {
  /* NOTE: not respecting endianness of host in these helpers. */
  return *(uint16_t*) p_in_buf;
}

static float
tape_read_float(uint8_t* p_in_buf) {
  return *(float*) p_in_buf;
}

void
tape_add_tape(struct tape_struct* p_tape, const char* p_file_name) {
  static const size_t k_max_uef_size = (1024 * 1024);
  uint8_t* p_in_file_buf;
  int32_t* p_out_file_buf;
  size_t len;
  size_t file_remaining;
  size_t buffer_remaining;
  uint8_t* p_in_buf;
  int32_t* p_out_buf;
  struct util_file* p_file;
  uint32_t num_tape_values;
  size_t tape_buffer_size;
  float temp_float;
  int32_t* p_tape_buffer;

  uint32_t tapes_added = p_tape->tapes_added;

  if (tapes_added == k_tape_max_tapes) {
    util_bail("too many tapes added");
  }

  p_in_file_buf = util_malloc(k_max_uef_size);
  p_out_file_buf = util_malloc(k_max_uef_size * 4);

  p_file = util_file_open(p_file_name, 0, 0);

  len = util_file_read(p_file, p_in_file_buf, k_max_uef_size);

  util_file_close(p_file);

  if (len == k_max_uef_size) {
    util_bail("uef file too large");
  }

  p_in_buf = p_in_file_buf;
  p_out_buf = p_out_file_buf;
  file_remaining = len;
  buffer_remaining = k_max_uef_size;
  if (file_remaining < 12) {
    util_bail("uef file missing header");
  }

  if ((p_in_buf[0] == 0x1F) && (p_in_buf[1] == 0x8B)) {
    util_bail("uef file needs unzipping first");
  }
  if (memcmp(p_in_buf, "UEF File!", 10) != 0) {
    util_bail("uef file incorrect header");
  }
  if (p_in_buf[11] != 0x00) {
    util_bail("uef file not supported, need major version 0");
  }
  p_in_buf += 12;
  file_remaining -= 12;

  while (file_remaining != 0) {
    uint16_t chunk_type;
    uint32_t chunk_len;
    uint16_t len_u16_1;
    uint16_t len_u16_2;
    uint32_t i;

    if (file_remaining < 6) {
      util_bail("uef file missing chunk");
    }
    chunk_type = (p_in_buf[1] << 8);
    chunk_type |= p_in_buf[0];
    chunk_len = (p_in_buf[5] << 24);
    chunk_len |= (p_in_buf[4] << 16);
    chunk_len |= (p_in_buf[3] << 8);
    chunk_len |= p_in_buf[2];
    p_in_buf += 6;
    file_remaining -= 6;
    if (chunk_len > file_remaining) {
      util_bail("uef file chunk too big");
    }

    switch (chunk_type) {
    case k_tape_uef_chunk_origin:
      /* A text comment, typically of which software made this UEF. */
      break;
    case k_tape_uef_chunk_data:
      if (chunk_len > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < chunk_len; ++i) {
        *p_out_buf++ = p_in_buf[i];
      }
      buffer_remaining -= chunk_len;
      break;
    case k_tape_uef_chunk_defined_format_data:
      if (chunk_len < 3) {
        util_bail("uef file short defined format chunk");
      }
      /* Read num data bits, then convert it to a mask. */
      len_u16_1 = p_in_buf[0];
      if ((len_u16_1 > 8) || (len_u16_1 < 1)) {
        util_bail("uef file bad number data bits");
      }
      len_u16_1 = ((1 << len_u16_1) - 1);
      /* NOTE: we ignore parity and stop bits. This is poor emulation, likely
       * giving incorrect timing. Also, I've yet to find it, but a nasty
       * protection could set up a tape vs. ACIA serial format mismatch and
       * rely on getting a framing error.
       */

      if ((chunk_len - 3) > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < (chunk_len - 3); ++i) {
        *p_out_buf++ = (p_in_buf[i + 3] & len_u16_1);
      }
      buffer_remaining -= (chunk_len - 3);
      break;
    case k_tape_uef_chunk_carrier_tone:
      if (chunk_len != 2) {
        util_bail("uef file incorrect carrier tone chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      /* From bits to 10-bit byte time units. */
      len_u16_1 /= 10;

      if (len_u16_1 > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_carrier;
      }
      buffer_remaining -= len_u16_1;
      break;
    case k_tape_uef_chunk_carrier_tone_with_dummy_byte:
      if (chunk_len != 4) {
        util_bail("uef file incorrect carrier tone with dummy byte chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      len_u16_2 = tape_read_u16(p_in_buf + 2);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      len_u16_2 >>= 1;
      /* From bits to 10-bit byte time units. */
      len_u16_1 /= 10;
      len_u16_2 /= 10;

      if ((uint32_t) (len_u16_1 + 1 + len_u16_2) > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_carrier;
      }
      *p_out_buf++ = 0xAA;
      for (i = 0; i < len_u16_2; ++i) {
        *p_out_buf++ = k_tape_uef_value_carrier;
      }
      buffer_remaining -= (len_u16_1 + 1 + len_u16_2);
      break;
    case k_tape_uef_chunk_gap_int:
      if (chunk_len != 2) {
        util_bail("uef file incorrect integer gap chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      /* From bits to 10-bit byte time units. */
      len_u16_1 /= 10;

      if (len_u16_1 > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_silence;
      }
      buffer_remaining -= len_u16_1;
      break;
    case k_tape_uef_chunk_set_baud_rate:
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_security_cycles:
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_phase_change:
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_gap_float:
      if (chunk_len != 4) {
        util_bail("uef file incorrect float gap chunk size");
      }
      temp_float = tape_read_float(p_in_buf);
      /* Current record: 263.9s, ChipBuster_B.hq.zip. */
      if ((temp_float > 360) || (temp_float < 0)) {
        util_bail("uef file strange float gap %f", temp_float);
      }
      len_u16_1 = (temp_float *
                       k_tape_system_tick_rate /
                       k_tape_ticks_per_byte);

      if (len_u16_1 > buffer_remaining) {
        util_bail("uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_silence;
      }
      buffer_remaining -= len_u16_1;
      break;
    default:
      util_bail("uef unknown chunk type 0x%.4"PRIx16, chunk_type);
      break;
    }

    p_in_buf += chunk_len;
    file_remaining -= chunk_len;
  }

  num_tape_values = (k_max_uef_size - buffer_remaining);
  tape_buffer_size = (num_tape_values * sizeof(int32_t));
  p_tape_buffer = util_malloc(tape_buffer_size);

  p_tape->p_tape_file_names[tapes_added] = util_strdup(p_file_name);
  p_tape->p_tape_buffers[tapes_added] = p_tape_buffer;
  p_tape->num_tape_values[tapes_added] = num_tape_values;

  (void) memcpy(p_tape_buffer, p_out_file_buf, tape_buffer_size);

  /* Always end with an empty slot. */
  p_tape->p_tape_buffers[tapes_added + 1] = NULL;
  p_tape->tapes_added++;

  util_free(p_in_file_buf);
  util_free(p_out_file_buf);
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
