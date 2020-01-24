#include "tape.h"

#include "bbc_options.h"
#include "serial.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_tape_system_tick_rate = 2000000,
  k_tape_bit_rate = 1200,
  k_tape_ticks_per_bit = (k_tape_system_tick_rate / k_tape_bit_rate),
  /* Includes a start and stop bit. */
  k_tape_ticks_per_byte = (k_tape_ticks_per_bit * 10),
};

enum {
  k_tape_uef_chunk_origin = 0x0000,
  k_tape_uef_chunk_data = 0x0100,
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

  uint32_t tick_rate;

  int32_t* p_tape_buffer;
  uint32_t num_tape_values;
  uint64_t tape_buffer_pos;
};

static void
tape_timer_callback(struct tape_struct* p_tape) {
  int32_t tape_value;
  int carrier = 0;

  (void) timing_set_timer_value(p_tape->p_timing,
                                p_tape->timer_id,
                                p_tape->tick_rate);

  if (p_tape->tape_buffer_pos < p_tape->num_tape_values) {
    tape_value = p_tape->p_tape_buffer[p_tape->tape_buffer_pos];
  } else {
    tape_value = k_tape_uef_value_silence;
  }

  if (tape_value == k_tape_uef_value_carrier) {
    carrier = 1;
  }
  serial_tape_set_carrier(p_tape->p_serial, carrier);
  if (tape_value >= 0) {
    serial_tape_receive_byte(p_tape->p_serial, (uint8_t) tape_value);
  }

  p_tape->tape_buffer_pos++;
}

struct tape_struct*
tape_create(struct timing_struct* p_timing,
            struct serial_struct* p_serial,
            struct bbc_options* p_options) {
  struct tape_struct* p_tape = malloc(sizeof(struct tape_struct));
  if (p_tape == NULL) {
    errx(1, "cannot allocate tape_struct");
  }

  (void) memset(p_tape, '\0', sizeof(struct tape_struct));

  p_tape->p_timing = p_timing;
  p_tape->p_serial = p_serial;

  p_tape->timer_id = timing_register_timer(p_timing,
                                           tape_timer_callback,
                                           p_tape);

  p_tape->tick_rate = k_tape_ticks_per_byte;
  (void) util_get_u32_option(&p_tape->tick_rate,
                             p_options->p_opt_flags,
                             "tape:tick-rate=");

  return p_tape;
}

void
tape_destroy(struct tape_struct* p_tape) {
  assert(!tape_is_playing(p_tape));
  if (p_tape->p_tape_buffer != NULL) {
    free(p_tape->p_tape_buffer);
  }
  free(p_tape);
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
tape_load(struct tape_struct* p_tape, const char* p_file_name) {
  static const size_t k_max_uef_size = (1024 * 1024);
  uint8_t in_buf[k_max_uef_size];
  int32_t out_buf[k_max_uef_size];
  size_t len;
  size_t file_remaining;
  size_t buffer_remaining;
  uint8_t* p_in_buf;
  int32_t* p_out_buf;
  intptr_t file_handle;
  uint32_t num_tape_values;
  size_t tape_buffer_size;
  float temp_float;

  assert(p_tape->p_tape_buffer == NULL);

  (void) memset(in_buf, '\0', sizeof(in_buf));
  (void) memset(out_buf, '\0', sizeof(out_buf));

  file_handle = util_file_handle_open(p_file_name, 0, 0);

  len = util_file_handle_read(file_handle, in_buf, sizeof(in_buf));

  util_file_handle_close(file_handle);

  if (len == sizeof(in_buf)) {
    errx(1, "uef file too large");
  }

  p_in_buf = in_buf;
  p_out_buf = out_buf;
  file_remaining = len;
  buffer_remaining = k_max_uef_size;
  if (file_remaining < 12) {
    errx(1, "uef file missing header");
  }

  if ((p_in_buf[0] == 0x1B) && (p_in_buf[1] == 0x8B)) {
    errx(1, "uef file needs unzipping first");
  }
  if (memcmp(p_in_buf, "UEF File!", 10) != 0) {
    errx(1, "uef file incorrect header");
  }
  if (p_in_buf[11] != 0x00) {
    errx(1, "uef file not supported, need major version 0");
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
      errx(1, "uef file missing chunk");
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
      errx(1, "uef file chunk too big");
    }

    switch (chunk_type) {
    case k_tape_uef_chunk_origin:
      /* A text comment, typically of which software made this UEF. */
      break;
    case k_tape_uef_chunk_data:
      if (chunk_len > buffer_remaining) {
        errx(1, "uef file out of buffer");
      }
      for (i = 0; i < chunk_len; ++i) {
        *p_out_buf++ = p_in_buf[i];
      }
      buffer_remaining -= chunk_len;
      break;
    case k_tape_uef_chunk_carrier_tone:
      if (chunk_len != 2) {
        errx(1, "uef file incorrect carrier tone chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      /* From bits to 10-bit byte time units. */
      len_u16_1 /= 10;

      if (len_u16_1 > buffer_remaining) {
        errx(1, "uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_carrier;
      }
      buffer_remaining -= len_u16_1;
      break;
    case k_tape_uef_chunk_carrier_tone_with_dummy_byte:
      if (chunk_len != 4) {
        errx(1, "uef file incorrect carrier tone with dummy byte chunk size");
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
        errx(1, "uef file out of buffer");
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
        errx(1, "uef file incorrect integer gap chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      /* From bits to 10-bit byte time units. */
      len_u16_1 /= 10;

      if (len_u16_1 > buffer_remaining) {
        errx(1, "uef file out of buffer");
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
        errx(1, "uef file incorrect float gap chunk size");
      }
      temp_float = tape_read_float(p_in_buf);
      /* Current record: 66.8s, STH 3DGrandPrix_B.hq.zip. */
      if ((temp_float > 120) || (temp_float < 0)) {
        errx(1, "uef file strange float gap %f", temp_float);
      }
      len_u16_1 = (temp_float *
                       k_tape_system_tick_rate /
                       k_tape_ticks_per_byte);

      if (len_u16_1 > buffer_remaining) {
        errx(1, "uef file out of buffer");
      }
      for (i = 0; i < len_u16_1; ++i) {
        *p_out_buf++ = k_tape_uef_value_silence;
      }
      buffer_remaining -= len_u16_1;
      break;
    default:
      errx(1, "uef unknown chunk type 0x%.4x", chunk_type);
      break;
    }

    p_in_buf += chunk_len;
    file_remaining -= chunk_len;
  }

  num_tape_values = (k_max_uef_size - buffer_remaining);
  tape_buffer_size = (num_tape_values * sizeof(int32_t));
  p_tape->p_tape_buffer = malloc(tape_buffer_size);
  if (p_tape->p_tape_buffer == NULL) {
    errx(1, "couldn't allocate tape buffer");
  }

  (void) memcpy(p_tape->p_tape_buffer, out_buf, tape_buffer_size);
  p_tape->num_tape_values = num_tape_values;
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
  serial_tape_set_carrier(p_tape->p_serial, 0);
}
