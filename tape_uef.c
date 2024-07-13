#include "tape_uef.h"

#include "log.h"
#include "tape.h"
#include "util.h"
#include "util_compress.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

enum {
  k_tape_uef_chunk_origin = 0x0000,
  k_tape_uef_chunk_target_machine = 0x0005,
  k_tape_uef_chunk_data = 0x0100,
  k_tape_uef_chunk_defined_format_data = 0x0104,
  k_tape_uef_chunk_carrier_tone = 0x0110,
  k_tape_uef_chunk_carrier_tone_with_dummy_byte = 0x0111,
  k_tape_uef_chunk_gap_int = 0x0112,
  k_tape_uef_chunk_set_baud_rate = 0x0113,
  k_tape_uef_chunk_security_cycles = 0x0114,
  k_tape_uef_chunk_phase_change = 0x0115,
  k_tape_uef_chunk_gap_float = 0x0116,
  k_tape_uef_chunk_data_encoding_format_change = 0x0117,
  k_tape_uef_chunk_position_marker = 0x0120,
};

static uint16_t
tape_read_u16(uint8_t* p_in_buf) {
  uint16_t ret = p_in_buf[1];
  ret <<= 8;
  ret |= p_in_buf[0];
  return ret;
}

static float
tape_read_float(uint8_t* p_in_buf) {
  return *(float*) p_in_buf;
}

void
tape_uef_load(struct tape_struct* p_tape,
              uint8_t* p_src,
              uint32_t src_len,
              int log_uef) {
  uint8_t* p_in_buf;
  uint32_t src_remaining;
  uint32_t baud_scale_for_300;
  int has_correct_parity = 0;
  uint8_t* p_deflate_buf = NULL;

  if (src_len < 2) {
    util_bail("UEF file too small");
  }

  p_in_buf = p_src;
  if ((p_in_buf[0] == 0x1F) && (p_in_buf[1] == 0x8B)) {
    int deflate_ret;
    size_t deflate_len = k_tape_max_file_size;
    p_deflate_buf = util_malloc(deflate_len);

    deflate_ret = util_gunzip(&deflate_len, p_in_buf, src_len, p_deflate_buf);
    if (deflate_ret != 0) {
      util_bail("UEF gunzip failed");
    }
    src_len = deflate_len;
    p_in_buf = p_deflate_buf;
  }

  if (src_len == k_tape_max_file_size) {
    util_bail("UEF file too large");
  }

  src_remaining = src_len;
  if (src_remaining < 12) {
    util_bail("UEF file missing header");
  }

  if (memcmp(p_in_buf, "UEF File!", 10) != 0) {
    util_bail("UEF file incorrect header");
  }
  if (p_in_buf[11] != 0x00) {
    util_bail("UEF file not supported, need major version 0");
  }
  p_in_buf += 12;
  src_remaining -= 12;

  baud_scale_for_300 = 1;
  while (src_remaining != 0) {
    uint16_t chunk_type;
    uint32_t chunk_len;
    uint16_t len_u16_1;
    uint16_t len_u16_2;
    uint32_t i;
    float temp_float;

    if (src_remaining < 6) {
      util_bail("UEF file missing chunk");
    }
    chunk_type = (p_in_buf[1] << 8);
    chunk_type |= p_in_buf[0];
    chunk_len = util_read_le32(&p_in_buf[2]);
    p_in_buf += 6;
    src_remaining -= 6;
    if (chunk_len > src_remaining) {
      util_bail("UEF file chunk too big");
    }

    if (log_uef) {
      log_do_log(k_log_tape,
                 k_log_info,
                 "UEF chunk $%.4X size %"PRIu32,
                 chunk_type,
                 chunk_len);
    }

    switch (chunk_type) {
    case k_tape_uef_chunk_origin:
      /* A text comment, typically of which software made this UEF. */
      if (log_uef) {
        p_in_buf[chunk_len - 1] = '\0';
        log_do_log(k_log_tape, k_log_info, "comment: %s", p_in_buf);
      }
      if ((chunk_len == 14) && !memcmp(p_in_buf, "MakeUEF V", 9)) {
        /* MakeUEF V2.4 or newer has the odd/even parity mixup fixed. */
        if (p_in_buf[9] > '2') {
          has_correct_parity = 1;
        }
        if ((p_in_buf[9] == '2') && (p_in_buf[11] >= '4')) {
          has_correct_parity = 1;
        }
      }
      break;
    case k_tape_uef_chunk_position_marker:
      /* Very uncommon! Found in EaglesWing-Smash7_B.zip from STH archive. */
      if (log_uef) {
        p_in_buf[chunk_len - 1] = '\0';
        log_do_log(k_log_tape, k_log_info, "position marker: %s", p_in_buf);
      }
      break;
    case k_tape_uef_chunk_target_machine:
      /* Uncommon. Found in Fortress-PIASRR_B.zip from the STH archive. */
      if (chunk_len != 1) {
        /* EaglesWing-Smash7_B.zip from STH archive has length 2. */
        log_do_log(k_log_tape,
                   k_log_warning,
                   "illegal target machine length: %"PRIu32,
                   chunk_len);
      }
      if (log_uef) {
        log_do_log(k_log_tape, k_log_info, "target machine: %d", p_in_buf[0]);
      }
      break;
    case k_tape_uef_chunk_data:
      if (log_uef) {
        char first_bytes[32];
        size_t filename_len;
        uint16_t block = 0;
        uint32_t copy_len = sizeof(first_bytes);
        (void) memset(&first_bytes[0], '\0', sizeof(first_bytes));
        if (copy_len > chunk_len) {
          copy_len = chunk_len;
        }
        (void) memcpy(&first_bytes[0], p_in_buf, copy_len);
        first_bytes[sizeof(first_bytes) - 1] = '\0';
        filename_len = strlen(&first_bytes[0]);
        if (filename_len < 12) {
          block = (first_bytes[filename_len + 10] << 8);
          block |= (first_bytes[filename_len + 9]);
        }
        log_do_log(k_log_tape,
                   k_log_info,
                   "standard data, file (maybe with sync byte) '%s' block %d",
                   &first_bytes[0],
                   block);
      }
      for (i = 0; i < chunk_len; ++i) {
        uint32_t i_bits;
        uint8_t byte = p_in_buf[i];
        /* Start bit. */
        tape_add_bits(p_tape, k_tape_bit_0, baud_scale_for_300);
        /* Data bits. */
        for (i_bits = 0; i_bits < 8; ++i_bits) {
          int bit = !!(byte & (1 << i_bits));
          if (bit) {
            tape_add_bits(p_tape, k_tape_bit_1, baud_scale_for_300);
          } else {
            tape_add_bits(p_tape, k_tape_bit_0, baud_scale_for_300);
          }
        }
        /* Stop bit. */
        tape_add_bits(p_tape, k_tape_bit_1, baud_scale_for_300);
      }
      break;
    case k_tape_uef_chunk_defined_format_data:
      if (chunk_len < 3) {
        util_bail("UEF file short defined format chunk");
      }

      /* Read num data bits, then convert it to a mask. */
      len_u16_1 = p_in_buf[0];
      if ((len_u16_1 != 8) && (len_u16_1 != 7)) {
        util_bail("UEF file bad number of data bits");
      }
      len_u16_2 = p_in_buf[2];
      if ((len_u16_2 != 1) && (len_u16_2 != 2)) {
        util_bail("UEF file bad number of stop bits");
      }
      if ((p_in_buf[1] != 'N') &&
          (p_in_buf[1] != 'O') &&
          (p_in_buf[1] != 'E')) {
        util_bail("UEF file bad parity");
      }

      if (log_uef) {
        char format[4];
        format[0] = ('0' + p_in_buf[0]);
        format[1] = p_in_buf[1];
        format[2] = ('0' + p_in_buf[2]);
        format[3] = '\0';
        log_do_log(k_log_tape,
                   k_log_info,
                   "defined data, format %s length %"PRIu32,
                   &format[0],
                   chunk_len);
      }

      for (i = 0; i < (chunk_len - 3); ++i) {
        uint32_t i_bits;
        int parity = 0;
        int emit_parity = 0;
        uint8_t byte = p_in_buf[i + 3];
        /* Start bit. */
        tape_add_bits(p_tape, k_tape_bit_0, baud_scale_for_300);
        /* Data bits. */
        for (i_bits = 0; i_bits < len_u16_1; ++i_bits) {
          int bit = !!(byte & (1 << i_bits));
          if (bit) {
            tape_add_bits(p_tape, k_tape_bit_1, baud_scale_for_300);
            parity = !parity;
          } else {
            tape_add_bits(p_tape, k_tape_bit_0, baud_scale_for_300);
          }
        }
        /* Parity bit. */
        /* NOTE: MakeUEF prior to V2.4 made UEFs with odd / even exactly
         * the wrong way around. For example, an 8O1 block is read with the
         * 6850 in 8E1 mode on the host. So we invert.
         */
        if (p_in_buf[1] == 'E') {
          emit_parity = 1;
        } else if (p_in_buf[1] == 'O') {
          emit_parity = 1;
          parity = !parity;
        }
        if (!has_correct_parity) {
          parity = !parity;
        }
        if (emit_parity) {
          if (parity) {
            tape_add_bits(p_tape, k_tape_bit_1, baud_scale_for_300);
          } else {
            tape_add_bits(p_tape, k_tape_bit_0, baud_scale_for_300);
          }
        }
        /* Stop bits. */
        for (i_bits = 0; i_bits < len_u16_2; ++i_bits) {
          tape_add_bits(p_tape, k_tape_bit_1, baud_scale_for_300);
        }
      }
      break;
    case k_tape_uef_chunk_carrier_tone:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect carrier tone chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;

      if (log_uef) {
        log_do_log(k_log_tape,
                   k_log_info,
                   "carrier tone, bits %"PRIu32,
                   len_u16_1);
      }

      tape_add_bits(p_tape, k_tape_bit_1, len_u16_1);
      break;
    case k_tape_uef_chunk_carrier_tone_with_dummy_byte:
      if (chunk_len != 4) {
        util_bail("UEF file incorrect carrier tone with dummy byte chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      len_u16_2 = tape_read_u16(p_in_buf + 2);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;
      len_u16_2 >>= 1;

      if (log_uef) {
        log_do_log(k_log_tape,
                   k_log_info,
                   "carrier tone with dummy byte, bits %"PRIu32", %"PRIu32,
                   len_u16_1,
                   len_u16_2);
      }

      tape_add_bits(p_tape, k_tape_bit_1, len_u16_1);
      tape_add_byte(p_tape, 0xAA);
      tape_add_bits(p_tape, k_tape_bit_1, len_u16_2);
      break;
    case k_tape_uef_chunk_gap_int:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect integer gap chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;

      if (log_uef) {
        log_do_log(k_log_tape, k_log_info, "gap, int value %"PRIu32, len_u16_1);
      }

      tape_add_bits(p_tape, k_tape_bit_silence, len_u16_1);
      break;
    case k_tape_uef_chunk_set_baud_rate:
      if (chunk_len != 4) {
        util_bail("UEF file incorrect baud rate chunk size");
      }
      temp_float = tape_read_float(p_in_buf);
      if (log_uef) {
        log_do_log(k_log_tape, k_log_info, "baud rate now %f", temp_float);
      }
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_security_cycles:
      if (log_uef) {
        log_do_log(k_log_tape, k_log_info, "security cycles");
      }
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_phase_change:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect phase change chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      if (log_uef) {
        log_do_log(k_log_tape,
                   k_log_info,
                   "phase now %"PRIu32,
                   len_u16_1);
      }
      /* Example file is STH 3DGrandPrix_B.hq.zip. */
      break;
    case k_tape_uef_chunk_gap_float:
      if (chunk_len != 4) {
        util_bail("UEF file incorrect float gap chunk size");
      }
      temp_float = tape_read_float(p_in_buf);
      if (log_uef) {
        log_do_log(k_log_tape, k_log_info, "gap, float value %f", temp_float);
      }
      /* Current record: 907.9s:
       * CompleteBBC,The(Audiogenic)[tape-2][side-1]_hq.uef.
       */
      if ((temp_float > 1200) || (temp_float < 0)) {
        util_bail("UEF file strange float gap %f", temp_float);
      }
      len_u16_1 = (temp_float * 1200);

      tape_add_bits(p_tape, k_tape_bit_silence, len_u16_1);
      break;
    case k_tape_uef_chunk_data_encoding_format_change:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect data encoding format chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      if (len_u16_1 == 1200) {
        baud_scale_for_300 = 1;
      } else if (len_u16_1 == 300) {
        baud_scale_for_300 = 4;
      } else {
        util_bail("UEF file unknown data encoding format");
      }

      if (log_uef) {
        log_do_log(k_log_tape,
                   k_log_info,
                   "data encoding format now %"PRIu32,
                   len_u16_1);
      }

      /* Example file is Swarm(ComputerConcepts).uef. */
      /* Some protected tape streams flip between 300 baud and 1200 baud.
       * Ignore it for now because we don't support different tape speeds. It
       * seems to work, but isn't a good emulation. It would be possible for a
       * tape loader to detect our subterfuge via either timing of tape bytes,
       * or by mismatching on-tape baud vs. serial baud and checking for
       * failure (we would succeed in passing the bytes along).
       */
      break;
    default:
      log_do_log(k_log_tape,
                 k_log_error,
                 "UEF unknown chunk type 0x%.4"PRIx16,
                 chunk_type);
      break;
    }

    p_in_buf += chunk_len;
    src_remaining -= chunk_len;
  }

  util_free(p_deflate_buf);
}
