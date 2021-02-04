#include "disc_dfi.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <string.h>

void
disc_dfi_load(struct disc_struct* p_disc,
              uint32_t capture_rev,
              int quantize_fm,
              int log_iffy_pulses,
              int is_skip_odd_tracks,
              int is_skip_upper_side) {
  static const size_t k_max_dfi_track_size = (1024 * 1024);
  uint32_t len;
  uint8_t header[4];
  uint8_t chunk[10];
  int32_t current_track;
  int32_t current_head;
  uint8_t* p_dfi_track_data;
  uint32_t num_sides = 1;

  struct util_file* p_file = disc_get_file(p_disc);

  /* TODO: honor these! */
  (void) is_skip_odd_tracks;
  (void) is_skip_upper_side;

  assert(p_file != NULL);

  len = util_file_read(p_file, &header[0], sizeof(header));
  if (len != sizeof(header)) {
    util_bail("DFI missing header");
  }

  if (memcmp(&header[0], "DFE2", 4) != 0) {
    util_bail("DFI bad header");
  }

  p_dfi_track_data = util_malloc(k_max_dfi_track_size);

  current_track = -1;
  current_head = -1;
  while (1) {
    uint16_t track;
    uint16_t head;
    uint16_t sector;
    uint32_t track_length;
    uint32_t i_data;
    uint16_t sample;
    int did_truncation_warning;
    uint32_t current_rev;

    len = util_file_read(p_file, &chunk[0], 10);
    if (len == 0) {
      /* No more chunks, done. */
      break;
    }
    if (len != 10) {
      util_bail("DFI can't read track chunk");
    }
    track = util_read_be16(&chunk[0]);
    head = util_read_be16(&chunk[2]);
    sector = util_read_be16(&chunk[4]);
    if (head == 0) {
      /* Start new track. */
      current_track++;
      current_head = 0;
      if (current_track >= k_ibm_disc_tracks_per_disc) {
        util_bail("DFI excessive tracks");
      }
    } else if (head == 1) {
      if (current_head != 0) {
        util_bail("DFI bad head");
      }
      current_head = 1;
      num_sides = 2;
    } else {
      util_bail("DFI head out of range");
    }
    if (track != current_track) {
      util_bail("DFI bad track");
    }
    /* The spec says this is always set to 0 for soft sectored discs but it
     * appears to be 1.
     */
    if (sector != 1) {
      util_bail("DFI hard sectored not supported");
    }
    track_length = util_read_be32(&chunk[6]);
    if (track_length > k_max_dfi_track_size) {
      util_bail("DFI track too large");
    }

    len = util_file_read(p_file, p_dfi_track_data, track_length);
    if (len != track_length) {
      util_bail("DFI can't read track data");
    }

    disc_build_track(p_disc, current_head, current_track);

    did_truncation_warning = 0;
    sample = 0;
    current_rev = 0;
    for (i_data = 0; i_data < track_length; ++i_data) {
      float delta_us;
      uint8_t byte = p_dfi_track_data[i_data];
      if ((byte & 0x80) != 0) {
        /* Index pulse. */
        current_rev++;
        sample += (byte & 0x7f);
        continue;
      }
      if (byte == 0x7f) {
        /* Carry. */
        sample += 0x7f;
        continue;
      }
      /* Emit sample. Counting units are 10 nanoseconds, but with a default
       * capture RPM of 360, not 300.
       */
      sample += byte;
      delta_us = (sample * (360.0 / 300.0) / 100.0);
      sample = 0;
      if (current_rev != capture_rev) {
        continue;
      }
      if (log_iffy_pulses) {
        if (!ibm_disc_format_check_pulse(delta_us, !quantize_fm)) {
          log_do_log(k_log_disc,
                     k_log_info,
                     "side %d track %d tpos %d iffy pulse %f (%s)",
                     current_head,
                     current_track,
                     i_data,
                     delta_us,
                     (quantize_fm ? "fm" : "mfm"));
        }
      }
      if (!disc_build_append_pulse_delta(p_disc, delta_us, !quantize_fm) &&
          !did_truncation_warning) {
        did_truncation_warning = 1;
        log_do_log(k_log_disc,
                   k_log_warning,
                   "DFI truncating side %d track %d",
                   current_head,
                   current_track);
      }
    }

    disc_build_set_track_length(p_disc);
  }

  log_do_log(k_log_disc,
             k_log_info,
             "DFI: found %d sides, %d tracks",
             num_sides,
             (current_track + 1));

  util_free(p_dfi_track_data);
}
