#include "disc_rfi.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t
disc_rfi_get_stanza(char* p_buf,
                    uint32_t max_len,
                    struct util_file* p_file) {
  uint32_t ret = 0;
  uint32_t len = 0;

  p_buf[max_len - 1] = 0;
  max_len--;

  while (len < max_len) {
    char val;
    uint32_t read_ret = util_file_read(p_file, &val, 1);
    if (read_ret == 0) {
      break;
    }
    assert(read_ret == 1);
    p_buf[len] = val;
    ++len;
    if (val == '}') {
      ret = len;
      break;
    }
  }

  p_buf[len] = 0;

  return ret;
}

void
disc_rfi_load(struct disc_struct* p_disc) {
  static const size_t k_max_rfi_track_size = (1024 * 1024);
  uint32_t i;
  char meta_buf[256];
  uint32_t len;
  char* p_buf;
  uint8_t* p_rfi_data;
  float* p_pulses;
  float rate_divider;
  uint32_t tracks = 0;
  uint32_t sides = 0;
  uint32_t rate = 0;

  struct util_file* p_file = disc_get_file(p_disc);
  assert(p_file != NULL);

  len = disc_rfi_get_stanza(&meta_buf[0], sizeof(meta_buf), p_file);
  if (len < 5) {
    util_bail("RFI header too short");
  }
  if (memcmp(meta_buf, "RFI{", 4) != 0) {
    util_bail("RFI header incorrect");
  }
  p_buf = strstr(meta_buf, "tracks:");
  if ((p_buf == NULL) || (sscanf(p_buf, "tracks:%"PRIu32, &tracks) != 1)) {
    util_bail("RFI can't get tracks");
  }
  p_buf = strstr(meta_buf, "sides:");
  if ((p_buf == NULL) || (sscanf(p_buf, "sides:%"PRIu32, &sides) != 1)) {
    util_bail("RFI can't get sides");
  }
  p_buf = strstr(meta_buf, "rate:");
  if ((p_buf == NULL) || (sscanf(p_buf, "rate:%"PRIu32, &rate) != 1)) {
    util_bail("RFI can't get rate");
  }

  if ((sides != 1) && (sides != 2)) {
    util_bail("RFI unsupported sides");
  }
  if ((tracks == 0) || (tracks > k_ibm_disc_tracks_per_disc)) {
    util_bail("RFI bad track count");
  }
  if ((rate < 5000000) || (rate > 50000000)) {
    util_bail("RFI wild rate");
  }

  rate_divider = (rate / 1000000.0);

  p_rfi_data = util_malloc(k_max_rfi_track_size);
  p_pulses = util_malloc(k_max_rfi_track_size * 4);

  for (i = 0; i < tracks; ++i) {
    uint32_t i_sides;
    for (i_sides = 0; i_sides < sides; ++i_sides) {
      uint32_t j;
      int level;
      uint32_t ticks_pos;
      uint32_t last_ticks_pulse_pos;
      uint32_t ticks_per_rev;
      uint32_t num_pulses;
      uint32_t curr_rev;
      float rpm = 0.0;
      uint32_t track = 0;
      uint32_t side = 0;
      uint32_t data_len = 0;

      len = disc_rfi_get_stanza(&meta_buf[0], sizeof(meta_buf), p_file);
      if (len == 0) {
        util_bail("RFI missing track %d", i);
      }
      p_buf = strstr(meta_buf, "track:");
      if ((p_buf == NULL) || (sscanf(p_buf, "track:%"PRIu32, &track) != 1)) {
        util_bail("RFI can't get track");
      }
      p_buf = strstr(meta_buf, "side:");
      if ((p_buf == NULL) || (sscanf(p_buf, "side:%"PRIu32, &side) != 1)) {
        util_bail("RFI can't get side");
      }
      p_buf = strstr(meta_buf, "len:");
      if ((p_buf == NULL) || (sscanf(p_buf, "len:%"PRIu32, &data_len) != 1)) {
        util_bail("RFI can't get len");
      }
      p_buf = strstr(meta_buf, "rpm:");
      if ((p_buf == NULL) || (sscanf(p_buf, "rpm:%f", &rpm) != 1)) {
        util_bail("RFI can't get rpm");
      }
      if (strstr(meta_buf, "enc:\"rle\"") == NULL) {
        util_bail("RFI encoding not rle");
      }
      if (track != i) {
        util_bail("RFI track mismatch");
      }
      if (side != i_sides) {
        util_bail("RFI sides mismatch");
      }
      if ((rpm < 200) || (rpm > 400)) {
        util_bail("RFI dodgy rpm");
      }

      if (data_len > k_max_rfi_track_size) {
        util_bail("RFI track data too big");
      }
      len = util_file_read(p_file, p_rfi_data, data_len);
      if (len != data_len) {
        util_bail("RFI track data EOF");
      }

      ticks_per_rev = (rate * (1.0 / (rpm / 60.0)));

      ticks_pos = 0;
      last_ticks_pulse_pos = 0;
      level = 0;
      j = 0;
      num_pulses = 0;
      curr_rev = 0;
      while (j < data_len) {
        uint32_t ticks_rev;
        uint32_t data = p_rfi_data[j];
        j++;
        if (data == 0xFF) {
          if ((j + 2) > data_len) {
            util_bail("RFI long run overread");
          }
          data = (p_rfi_data[j] * 256);
          data += p_rfi_data[j + 1];
          data += 255;
          j += 2;
        }
        ticks_pos += data;
        ticks_rev = (ticks_pos / ticks_per_rev);
        if (ticks_rev != curr_rev) {
          disc_build_track_from_pulses(p_disc,
                                       curr_rev,
                                       i_sides,
                                       i,
                                       p_pulses,
                                       num_pulses);
          num_pulses = 0;
          curr_rev = ticks_rev;
        }

        level = !level;
        if (level) {
          float delta_us = (ticks_pos - last_ticks_pulse_pos);
          delta_us /= rate_divider;
          p_pulses[num_pulses] = delta_us;
          num_pulses++;
          last_ticks_pulse_pos = ticks_pos;
        }
      }
      if (j == data_len) {
        log_do_log(k_log_disc, k_log_warning, "RFI data ran out track %d", i);
      }
    } /* End of sides loop. */
  } /* End of track loop. */

  util_free(p_rfi_data);
  util_free(p_pulses);
}
