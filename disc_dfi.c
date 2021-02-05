#include "disc_dfi.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <string.h>

void
disc_dfi_load(struct disc_struct* p_disc) {
  static const size_t k_max_dfi_track_size = (1024 * 1024);
  uint32_t len;
  uint8_t header[4];
  uint8_t chunk[10];
  int32_t current_track;
  int32_t current_head;
  uint8_t* p_dfi_track_data;
  float* p_pulses;
  uint32_t num_sides = 1;

  struct util_file* p_file = disc_get_file(p_disc);

  assert(p_file != NULL);

  len = util_file_read(p_file, &header[0], sizeof(header));
  if (len != sizeof(header)) {
    util_bail("DFI missing header");
  }

  if (memcmp(&header[0], "DFE2", 4) != 0) {
    util_bail("DFI bad header");
  }

  p_dfi_track_data = util_malloc(k_max_dfi_track_size);
  p_pulses = util_malloc(k_max_dfi_track_size * 4);

  current_track = -1;
  current_head = -1;
  while (1) {
    uint16_t track;
    uint16_t head;
    uint16_t sector;
    uint32_t track_length;
    uint32_t i_data;
    uint16_t sample;
    uint32_t current_rev;
    uint32_t num_pulses;

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

    sample = 0;
    current_rev = 0;
    num_pulses = 0;
    for (i_data = 0; i_data < track_length; ++i_data) {
      float delta_us;
      uint8_t byte = p_dfi_track_data[i_data];
      if ((byte & 0x80) != 0) {
        /* Index pulse. */
        disc_build_track_from_pulses(p_disc,
                                     current_rev,
                                     current_head,
                                     current_track,
                                     p_pulses,
                                     num_pulses);
        current_rev++;
        num_pulses = 0;
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
      p_pulses[num_pulses] = delta_us;
      num_pulses++;
    }
  }

  log_do_log(k_log_disc,
             k_log_info,
             "DFI: found %d sides, %d tracks",
             num_sides,
             (current_track + 1));

  util_free(p_dfi_track_data);
  util_free(p_pulses);
}
