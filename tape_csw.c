#include "tape_csw.h"

#include "log.h"
#include "tape.h"
#include "util.h"
#include "util_compress.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

static int
tape_csw_is_half_1200(uint32_t half_wave) {
  /* 1200Hz +- 30% */
  /* Castle Quest-Micro Power.csw needs 14 to be included to load. */
  return ((half_wave >= 14) && (half_wave <= 26));
}

static int
tape_csw_is_half_2400(uint32_t half_wave) {
  /* 2400Hz +- 25% */
  /* Joust-Aardvark.csw needs 12 to be included to load. */
  return ((half_wave >= 7) && (half_wave <= 12));
}

static uint32_t
tape_csw_get_next_half_wave(uint32_t* p_len_consumed,
                            uint8_t* p_buf,
                            uint32_t len) {
  uint8_t byte;

  assert(len > 0);
  *p_len_consumed = 1;
  byte = p_buf[0];

  if (byte != 0) {
    return byte;
  }

  /* If it fits, it's a zero byte followed by a 4-byte length. */
  if (len < 5) {
    return byte;
  }

  *p_len_consumed = 5;
  return util_read_le32(&p_buf[1]);
}

static void
tape_csw_get_next_bit(int* p_bit,
                      uint32_t* p_bit_bytes,
                      uint32_t* p_bit_samples,
                      uint32_t* p_half_wave,
                      uint32_t* p_half_wave2,
                      uint32_t* p_half_wave3,
                      uint32_t* p_half_wave4,
                      uint8_t* p_buf,
                      uint32_t len) {
  uint32_t len_consumed;
  uint32_t len_consumed_total = 0;

  assert(len > 0);

  *p_bit = k_tape_bit_silence;
  *p_half_wave = tape_csw_get_next_half_wave(&len_consumed, p_buf, len);
  *p_half_wave2 = 0;
  *p_half_wave3 = 0;
  *p_half_wave4 = 0;
  *p_bit_bytes = len_consumed;
  *p_bit_samples = *p_half_wave;
  len -= len_consumed;
  p_buf += len_consumed;
  len_consumed_total += len_consumed;

  if (len == 0) {
    return;
  }
  *p_half_wave2 = tape_csw_get_next_half_wave(&len_consumed, p_buf, len);
  len -= len_consumed;
  p_buf += len_consumed;
  len_consumed_total += len_consumed;

  if (tape_csw_is_half_1200(*p_half_wave) &&
      tape_csw_is_half_1200(*p_half_wave2)) {
    *p_bit = k_tape_bit_0;
    *p_bit_bytes = len_consumed_total;
    *p_bit_samples = (*p_half_wave + *p_half_wave2);
    return;
  }

  if (len == 0) {
    return;
  }
  *p_half_wave3 = tape_csw_get_next_half_wave(&len_consumed, p_buf, len);
  len -= len_consumed;
  p_buf += len_consumed;
  len_consumed_total += len_consumed;
  if (len == 0) {
    return;
  }
  *p_half_wave4 = tape_csw_get_next_half_wave(&len_consumed, p_buf, len);
  len -= len_consumed;
  p_buf += len_consumed;
  len_consumed_total += len_consumed;

  if (tape_csw_is_half_2400(*p_half_wave) &&
      tape_csw_is_half_2400(*p_half_wave2) &&
      tape_csw_is_half_2400(*p_half_wave3) &&
      tape_csw_is_half_2400(*p_half_wave4)) {
    *p_bit = k_tape_bit_1;
    *p_bit_bytes = len_consumed_total;
    *p_bit_samples =
        (*p_half_wave + *p_half_wave2 + *p_half_wave3 + *p_half_wave4);
  }
}

void
tape_csw_load(struct tape_struct* p_tape,
              uint8_t* p_src,
              uint32_t src_len,
              int do_check_bits) {
  /* The CSW file format: http://ramsoft.bbk.org.omegahg.com/csw.html */
  uint8_t* p_in_buf;
  uint8_t extension_len;
  uint32_t requested_sample_rate;
  uint32_t i_waves;
  uint32_t carrier_count;
  uint32_t data_one_bits_count;
  uint32_t ticks;
  int is_silence;
  int is_carrier;
  uint32_t data_len;
  uint32_t samples;
  uint32_t bit_index;
  uint32_t byte_wave_start;
  uint32_t byte_sample_start;
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

  samples = 0;
  is_silence = 1;
  is_carrier = 0;
  ticks = 0;
  carrier_count = 0;
  bit_index = 0;
  data_one_bits_count = 0;
  byte_wave_start = 0;
  byte_sample_start = 0;

  i_waves = 0;
  while (i_waves < data_len) {
    int bit;
    uint32_t bit_bytes;
    uint32_t bit_samples;
    uint32_t half_wave;
    uint32_t half_wave2;
    uint32_t half_wave3;
    uint32_t half_wave4;

    tape_csw_get_next_bit(&bit,
                          &bit_bytes,
                          &bit_samples,
                          &half_wave,
                          &half_wave2,
                          &half_wave3,
                          &half_wave4,
                          (p_in_buf + i_waves),
                          (data_len - i_waves));

    /* Run a little state machine that bounces between silence, carrier and
     * data. It's important to track when we're in carrier because the carrier
     * signal often doesn't last for an exact number of bits widths.
     */
    if (is_silence) {
      if (bit == k_tape_bit_1) {
        /* Transition, silence to carrier. */
        uint32_t num_bits = (ticks / (44100 / 1200));
        /* Make sure silence is always seen even if we rounded down. */
        num_bits++;
        tape_add_bits(p_tape, k_tape_bit_silence, num_bits);
        is_silence = 0;
        is_carrier = 1;
        carrier_count = 1;
      } else {
        /* Still silence. */
        ticks += bit_samples;
      }
    } else if (is_carrier) {
      if ((bit == k_tape_bit_1) || tape_csw_is_half_2400(half_wave)) {
        assert(bit != k_tape_bit_0);
        /* Still carrier. */
        if (bit == k_tape_bit_1) {
          carrier_count++;
        }
      } else {
        tape_add_bits(p_tape, k_tape_bit_1, carrier_count);
        is_carrier = 0;
        if (bit == k_tape_bit_0) {
          /* Transition, carrier to data. We've found the start bit. */
          tape_add_bit(p_tape, k_tape_bit_0);
          bit_index = 1;
          byte_wave_start = i_waves;
          byte_sample_start = samples;
          data_one_bits_count = 0;
        } else {
          /* Transition, carrier to silence. */
          assert(bit == k_tape_bit_silence);
          is_silence = 1;
          ticks = bit_samples;
        }
      }
    } else {
      /* In data. */
      if (bit != k_tape_bit_silence) {
        tape_add_bit(p_tape, bit);
        if (bit == k_tape_bit_0) {
          data_one_bits_count = 0;
        } else {
          data_one_bits_count++;
          if (data_one_bits_count == 10) {
            /* Transition, data to carrier. */
            is_carrier = 1;
            carrier_count = 0;
          }
        }
        if (bit_index == 0) {
          byte_wave_start = i_waves;
          byte_sample_start = samples;
          bit_index = 1;
        } else if (bit_index == 9) {
          if (do_check_bits && (bit != k_tape_bit_1)) {
            log_do_log(k_log_tape,
                       k_log_info,
                       "CSW bad stop bit"
                       ", byte start %"PRIu32
                       ", sample %"PRIu32,
                       (byte_wave_start + 0x34),
                       byte_sample_start);
          }
          bit_index = 0;
        } else {
          bit_index++;
        }
      } else {
        /* Transition, data to silence. This sometimes indicates a wobble in
         * the CSW that would lead to a load failure.
         */
        log_do_log(k_log_tape,
                   k_log_info,
                   "CSW data to silence"
                   ", byte %"PRIu32
                   ", sample %"PRIu32
                   " (%d %d %d %d)",
                   (i_waves + 0x34),
                   samples,
                   half_wave,
                   half_wave2,
                   half_wave3,
                   half_wave4);
        is_silence = 1;
        ticks = bit_samples;
      }
    }

    samples += bit_samples;
    i_waves += bit_bytes;
  }

  util_free(p_uncompress_buf);
}
