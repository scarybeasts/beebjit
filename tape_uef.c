#include "tape_uef.h"

#include "tape.h"
#include "util.h"
#include "util_compress.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

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
  k_tape_uef_chunk_data_encoding_format_change = 0x0117,
};

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
tape_uef_load(struct tape_struct* p_tape, uint8_t* p_src, uint32_t src_len) {
  uint8_t* p_in_buf;
  uint32_t src_remaining;
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

    switch (chunk_type) {
    case k_tape_uef_chunk_origin:
      /* A text comment, typically of which software made this UEF. */
      break;
    case k_tape_uef_chunk_data:
      for (i = 0; i < chunk_len; ++i) {
        tape_add_byte(p_tape, p_in_buf[i]);
      }
      break;
    case k_tape_uef_chunk_defined_format_data:
      if (chunk_len < 3) {
        util_bail("UEF file short defined format chunk");
      }
      /* Read num data bits, then convert it to a mask. */
      len_u16_1 = p_in_buf[0];
      if ((len_u16_1 > 8) || (len_u16_1 < 1)) {
        util_bail("UEF file bad number data bits");
      }
      len_u16_1 = ((1 << len_u16_1) - 1);
      /* NOTE: we ignore parity and stop bits. This is poor emulation, likely
       * giving incorrect timing. Also, I've yet to find it, but a nasty
       * protection could set up a tape vs. ACIA serial format mismatch and
       * rely on getting a framing error.
       */
      for (i = 0; i < (chunk_len - 3); ++i) {
        uint8_t byte = (p_in_buf[i + 3] & len_u16_1);
        tape_add_byte(p_tape, byte);
      }
      break;
    case k_tape_uef_chunk_carrier_tone:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect carrier tone chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;

      tape_add_carrier_bits(p_tape, len_u16_1);
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

      tape_add_carrier_bits(p_tape, len_u16_1);
      tape_add_byte(p_tape, 0xAA);
      tape_add_carrier_bits(p_tape, len_u16_2);
      break;
    case k_tape_uef_chunk_gap_int:
      if (chunk_len != 2) {
        util_bail("UEF file incorrect integer gap chunk size");
      }
      len_u16_1 = tape_read_u16(p_in_buf);
      /* Length is specified in terms of 2x time units per baud. */
      len_u16_1 >>= 1;

      tape_add_silence_bits(p_tape, len_u16_1);
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
        util_bail("UEF file incorrect float gap chunk size");
      }
      temp_float = tape_read_float(p_in_buf);
      /* Current record: 907.9s:
       * CompleteBBC,The(Audiogenic)[tape-2][side-1]_hq.uef.
       */
      if ((temp_float > 1200) || (temp_float < 0)) {
        util_bail("UEF file strange float gap %f", temp_float);
      }
      len_u16_1 = (temp_float * 1200);

      tape_add_silence_bits(p_tape, len_u16_1);
      break;
    case k_tape_uef_chunk_data_encoding_format_change:
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
      util_bail("UEF unknown chunk type 0x%.4"PRIx16, chunk_type);
      break;
    }

    p_in_buf += chunk_len;
    src_remaining -= chunk_len;
  }

  util_free(p_deflate_buf);
}
