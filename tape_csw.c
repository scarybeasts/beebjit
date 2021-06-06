#include "tape_csw.h"

#include "log.h"
#include "tape.h"
#include "util.h"
#include "util_compress.h"

#include <inttypes.h>
#include <string.h>

static int
tape_csw_is_half_1200(uint8_t half_wave) {
  /* 1200Hz +- 30% */
  /* Castle Quest-Micro Power.csw needs 14 to be included to load. */
  return ((half_wave >= 14) && (half_wave <= 26));
}

static int
tape_csw_is_half_2400(uint8_t half_wave) {
  /* 2400Hz +- 25% */
  /* Joust-Aardvark.csw needs 12 to be included to load. */
  return ((half_wave >= 7) && (half_wave <= 12));
}

void
tape_csw_load(struct tape_struct* p_tape,
              uint8_t* p_src,
              uint32_t src_len) {
  /* The CSW file format: http://ramsoft.bbk.org.omegahg.com/csw.html */
  uint8_t* p_in_buf;
  uint8_t extension_len;
  uint32_t requested_sample_rate;
  uint32_t i_waves;
  uint8_t data_byte;
  uint32_t data_bit_pos;
  uint32_t carrier_count;
  uint32_t wave_bytes;
  uint32_t ticks;
  int is_silence;
  int is_carrier;
  uint32_t data_len;
  uint32_t sample_rate = 44100;
  uint8_t* p_uncompress_buf = NULL;

  if (src_len < 0x34) {
    util_bail("CSW file too small");
  }
  if (memcmp(p_src, "Compressed Square Wave", 22) != 0) {
    util_bail("CSW file incorrect header");
  }
  if (p_src[0x16] != 0x1A) {
    util_bail("CSW file incorrect terminator code");
  }
  if (p_src[0x17] != 0x02) {
    util_bail("CSW file not version 2");
  }
  requested_sample_rate = util_read_le32(&p_src[0x19]);
  if (requested_sample_rate != sample_rate) {
    util_bail("CSW file sample rate not supported: %d"PRIu32,
              requested_sample_rate);
  }
  if ((p_src[0x21] != 0x01) && (p_src[0x21] != 0x02)) {
    util_bail("CSW file compression not RLE or Z-RLE");
  }
  extension_len = p_src[0x23];

  p_in_buf = p_src;
  p_in_buf += 0x34;
  src_len -= 0x34;

  if (extension_len > src_len) {
    util_bail("CSW file extension doesn't fit");
  }
  p_in_buf += extension_len;
  src_len -= extension_len;

  if (p_src[0x21] == 0x01) {
    data_len = src_len;
  } else {
    int uncompress_ret;
    size_t uncompress_len = (k_tape_max_file_size * 16);
    p_uncompress_buf = util_malloc(uncompress_len);

    uncompress_ret = util_uncompress(&uncompress_len,
                                     p_in_buf,
                                     src_len,
                                     p_uncompress_buf);
    if (uncompress_ret != 0) {
      util_bail("CSW uncompress failed");
    }
    if (uncompress_len == (k_tape_max_file_size * 16)) {
      util_bail("CSW uncompress too large");
    }

    p_in_buf = p_uncompress_buf;
    data_len = uncompress_len;
  }

  is_silence = 1;
  is_carrier = 0;
  ticks = 0;
  carrier_count = 0;
  data_byte = 0;
  data_bit_pos = 0;
  for (i_waves = 0; i_waves < (data_len - 3); i_waves += wave_bytes) {
    uint8_t half_wave = p_in_buf[i_waves];
    uint8_t next_half_wave = p_in_buf[i_waves + 1];
    int wave_is_1200 = 0;
    int wave_is_2400 = 0;
    wave_bytes = 1;
    /* Look ahead to see if we have noise, or 1 1200Hz cycle, or 2 2400Hz
     * cycles.
     */
    if (tape_csw_is_half_1200(half_wave) &&
        tape_csw_is_half_1200(next_half_wave)) {
      wave_is_1200 = 1;
      wave_bytes = 2;
    } else if (tape_csw_is_half_2400(half_wave) &&
               tape_csw_is_half_2400(next_half_wave) &&
               tape_csw_is_half_2400(p_in_buf[i_waves + 2]) &&
               tape_csw_is_half_2400(p_in_buf[i_waves + 3])) {
      wave_is_2400 = 1;
      wave_bytes = 4;
    }

    /* Run a little state machine that bounces between silence, carrier and
     * data.
     */
    if (is_silence) {
      if (wave_is_2400) {
        /* Transition, silence to carrier. */
        uint32_t num_bits = (ticks / (44100 / 1200));
        /* Make sure silence is always seen even if we rounded down. */
        num_bits++;
        tape_add_silence_bits(p_tape, num_bits);
        is_silence = 0;
        is_carrier = 1;
        carrier_count = 1;
      } else {
        /* Still silence. */
        ticks += half_wave;
        wave_bytes = 1;
      }
    } else if (is_carrier) {
      if (wave_is_2400 || tape_csw_is_half_2400(half_wave)) {
        /* Still carrier. */
        if (wave_is_2400) {
          carrier_count++;
        }
      } else {
        tape_add_carrier_bits(p_tape, carrier_count);
        is_carrier = 0;
        if (wave_is_1200) {
          /* Transition, carrier to data. We've found the start bit. */
          data_byte = 0;
          data_bit_pos = 0;
        } else {
          /* Transition, carrier to silence. */
          is_silence = 1;
          ticks = half_wave;
        }
      }
    } else {
      /* In data. */
      if (!wave_is_1200 && !wave_is_2400) {
        /* Transition, data to silence. */
        is_silence = 1;
        ticks = half_wave;
      } else {
        if (data_bit_pos < 8) {
          /* We always look to gather 8-bit data chunks.
           * A more accurate emulation would just provide a bit stream and
           * let the 6850 sort it out (including framing and parity errors)
           * based on the mode it's in.
           * Gathering 8-bit data chunks seems to work even though parity /
           * stop bits etc. can vary. We can use heuristics to work out which
           * it must be by looking for stop vs. start bits.
           */
          if (wave_is_2400) {
            data_byte |= (1 << data_bit_pos);
          }
          data_bit_pos++;
          if (data_bit_pos == 8) {
            tape_add_byte(p_tape, data_byte);
          }
        } else if (data_bit_pos == 8) {
          /* Could be a parity bit or a stop bit. Skip. */
          data_bit_pos++;
        } else {
          /* Could be a start bit, a stop bit or return to carrier. */
          if (wave_is_1200) {
            /* It's a start bit. */
            data_byte = 0;
            data_bit_pos = 0;
          } else {
            if (data_bit_pos == 10) {
              /* Transition, data to carrier.
               * Has to be return to carrier; max stop bits is 2.
               */
              is_carrier = 1;
              carrier_count = 1;
            } else {
              data_bit_pos++;
            }
          }
        }
      }
    }
  }

  util_free(p_uncompress_buf);
}
