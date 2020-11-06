#include "disc_kryo.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t k_kryo_max_index_pulses = 16;

void
disc_kryo_load(struct disc_struct* p_disc,
               const char* p_full_file_name,
               uint32_t capture_rev,
               int log_iffy_pulses) {
  static const size_t k_max_kryo_track_size = (1024 * 1024);
  uint32_t i_track;
  uint8_t* p_raw_buf;
  char* p_file_name_base = NULL;
  char* p_file_name = NULL;
  struct util_file* p_extra_file = NULL;
  struct util_file* p_file = disc_get_file(p_disc);
  struct util_file* p_track_file = p_file;

  assert(p_file != NULL);

  util_file_name_split(&p_file_name_base, &p_file_name, p_full_file_name);
  if (strcmp(p_file_name, "track00.0.raw") != 0) {
    util_bail("Kryo filename must be track00.0.raw");
  }
  util_free(p_file_name);

  p_raw_buf = util_malloc(k_max_kryo_track_size);

  i_track = 0;
  while (i_track < k_ibm_disc_tracks_per_disc) {
    uint32_t i_pass;
    uint32_t data_len;
    uint32_t index_pulse_indexes[k_kryo_max_index_pulses];
    uint32_t num_index_pulses = 0;
    uint32_t next_index_pulse = 0;

    if (i_track > 0) {
      char file_name_buf[32];
      char* p_extra_file_name;

      if (p_extra_file != NULL) {
        util_file_close(p_extra_file);
      }

      (void) snprintf(file_name_buf,
                      sizeof(file_name_buf),
                      "track%02d.0.raw",
                      i_track);
      p_extra_file_name = util_file_name_join(p_file_name_base,
                                              &file_name_buf[0]);
      p_extra_file = util_file_try_read_open(p_extra_file_name);
      util_free(p_extra_file_name);
      if (p_extra_file == NULL) {
        /* Finished if the next track file doesn't exist. */
        break;
      }
      p_track_file = p_extra_file;
    }

    data_len = util_file_read(p_track_file, p_raw_buf, k_max_kryo_track_size);
    if (data_len == k_max_kryo_track_size) {
      util_bail("Kryo track file too large");
    }

    disc_build_track(p_disc, 0, i_track);

    /* Two passes because the index pulses are reported asynchronously in the
     * data stream.
     */
    for (i_pass = 0; i_pass < 2; ++i_pass) {
      int32_t sample_value = -1;
      uint32_t ticks_pos = 0;
      uint32_t i_data = 0;
      uint32_t i_samples = 0;
      int writing = 0;

      while (i_data < data_len) {
        uint32_t chunk_len;
        uint8_t val = p_raw_buf[i_data];

        if ((i_pass == 1) &&
            (next_index_pulse < num_index_pulses) &&
            (i_samples >= index_pulse_indexes[next_index_pulse])) {
          if (next_index_pulse == capture_rev) {
            writing = 1;
          } else {
            writing = 0;
          }
          next_index_pulse++;
        }

        switch (val) {
        /* Special chunk. */
        case 0x0D:
          if (i_data == (data_len - 1)) {
            util_bail("Kryo no chunk type");
          }
          i_data++;
          val = p_raw_buf[i_data];
          i_data++;
          switch (val) {
          /* EOF. */
          case 0x0D:
            i_data = data_len;
            break;
          default:
            if ((i_data + 2) > data_len) {
              util_bail("Kryo chunk len doesn't fit");
            }
            chunk_len = p_raw_buf[i_data];
            chunk_len += (p_raw_buf[i_data + 1] * 256);
            i_data += 2;
            if ((i_data + chunk_len) > data_len) {
              util_bail("Kryo chunk doesn't fit");
            }
            /* Index. */
            if (val == 0x02) {
              if (chunk_len != 12) {
                util_bail("Kryo bad index chunk size");
              }
              if (i_pass == 0) {
                uint32_t index_pulse_index;
                if (num_index_pulses == k_kryo_max_index_pulses) {
                  util_bail("Kryo too many index pulses");
                }
                index_pulse_index = p_raw_buf[i_data];
                index_pulse_index += (p_raw_buf[i_data + 1] << 8);
                index_pulse_index += (p_raw_buf[i_data + 2] << 16);
                index_pulse_index += (p_raw_buf[i_data + 3] << 24);
                index_pulse_indexes[num_index_pulses] = index_pulse_index;
                num_index_pulses++;
              }
            }
            i_data += chunk_len;
            break;
          }
          break;
        case 0x08:
        case 0x09:
        case 0x0A:
          i_data++;
          i_samples++;
          if ((i_data + (val - 0x08)) > data_len) {
            util_bail("Kryo nop doesn't fit");
          }
          i_data += (val - 0x08);
          i_samples += (val - 0x08);
          break;
        case 0x0B:
          util_bail("Kryo +65536");
          break;
        case 0x0C:
          util_bail("Kryo 16-bit");
        break;
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
          if ((i_data + 2) > data_len) {
            util_bail("Kryo 2 byte sample doesn't fit");
          }
          sample_value = (val * 256);
          sample_value += p_raw_buf[i_data + 1];
          i_data += 2;
          i_samples += 2;
          break;
        /* 1-byte sample. */
        default:
          sample_value = val;
          i_data++;
          i_samples++;
          break;
        }

        if (writing && (sample_value != -1)) {
          float delta_us = (sample_value / 24.027428);
          ticks_pos += sample_value;
          if (log_iffy_pulses) {
            if ((delta_us < 3.5) || (delta_us > 8.5)) {
              log_do_log(k_log_disc,
                         k_log_info,
                         "track %d pos %d dpos %d iffy pulse %f",
                         i_track,
                         ticks_pos,
                         i_data,
                         delta_us);
            }
          }
          if (!disc_build_append_pulse_delta(p_disc, delta_us)) {
            log_do_log(k_log_disc,
                       k_log_warning,
                       "Kryo truncating track %d",
                       i_track);
          }
          sample_value = -1;
        }
      } /* end: data loop. */
    } /* end: passes loop. */

    disc_build_set_track_length(p_disc);

    i_track++;
  }

  log_do_log(k_log_disc, k_log_info, "KryoFlux raw, loaded %d tracks", i_track);

  util_free(p_raw_buf);
  if (p_file_name_base != NULL) {
    util_free(p_file_name_base);
  }
}
