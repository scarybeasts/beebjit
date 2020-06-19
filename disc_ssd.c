#include "disc_ssd.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "util.h"

#include <assert.h>
#include <string.h>

enum {
  k_disc_ssd_sector_size = 256,
  k_disc_ssd_sectors_per_track = 10,
  k_disc_ssd_tracks_per_disc = 80,
};

void
disc_ssd_write_track(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t length,
                     uint8_t* p_data,
                     uint8_t* p_clocks) {
  uint32_t i;
  uint64_t seek_pos;
  int32_t sector = -1;

  assert(length == k_ibm_disc_bytes_per_track);

  struct util_file* p_file = disc_get_file(p_disc);
  uint32_t track_size = (k_disc_ssd_sector_size * k_disc_ssd_sectors_per_track);
  int is_dsd = disc_is_double_sided(p_disc);

  seek_pos = (track_size * track);
  if (is_dsd) {
    seek_pos *= 2;
  }
  if (is_side_upper) {
    assert(is_dsd);
    seek_pos += track_size;
  }

  for (i = 0; i < length; ++i) {
    if ((i + 1 + k_disc_ssd_sector_size) > length) {
      break;
    }
    if (p_clocks[i] != k_ibm_disc_mark_clock_pattern) {
      continue;
    }
    if (p_data[i] == k_ibm_disc_id_mark_data_pattern) {
      sector = p_data[i + 3];
      continue;
    }
    if (p_data[i] != k_ibm_disc_data_mark_data_pattern) {
      continue;
    }
    if ((sector < 0) || (sector > 9)) {
      continue;
    }

    util_file_seek(p_file, (seek_pos + (sector * k_disc_ssd_sector_size)));
    i += 1;
    util_file_write(p_file, &p_data[i], k_disc_ssd_sector_size);
    i += k_disc_ssd_sector_size;
  }
}

void
disc_ssd_load(struct disc_struct* p_disc, int is_dsd) {
  static const uint32_t k_max_ssd_size = (k_disc_ssd_sector_size *
                                          k_disc_ssd_sectors_per_track *
                                          k_disc_ssd_tracks_per_disc *
                                          2);
  uint64_t file_size;
  size_t read_ret;
  uint8_t* p_file_buf;
  uint32_t i_side;
  uint32_t i_track;
  uint32_t i_sector;

  struct util_file* p_file = disc_get_file(p_disc);
  uint8_t* p_ssd_data;
  uint32_t num_sides = 2;
  uint32_t max_size = k_max_ssd_size;

  assert(p_file != NULL);

  disc_set_is_double_sided(p_disc, is_dsd);

  /* Must zero it out because it is all read even if the file is short. */
  p_file_buf = util_mallocz(k_max_ssd_size);
  p_ssd_data = p_file_buf;

  if (!is_dsd) {
    max_size /= 2;
    num_sides = 1;
  }
  file_size = util_file_get_size(p_file);
  if (file_size > max_size) {
    util_bail("ssd/dsd file too large");
  }
  if ((file_size % k_disc_ssd_sector_size) != 0) {
    util_bail("ssd/dsd file not a sector multiple");
  }

  read_ret = util_file_read(p_file, p_file_buf, file_size);
  if (read_ret != file_size) {
    util_bail("ssd/dsd file short read");
  }

  for (i_track = 0; i_track < k_disc_ssd_tracks_per_disc; ++i_track) {
    for (i_side = 0; i_side < num_sides; ++i_side) {
      disc_build_track(p_disc, i_side, i_track);
      /* Sync pattern at start of track, as the index pulse starts, aka.
       * GAP 5.
       */
      disc_build_append_repeat(p_disc, 0xFF, k_ibm_disc_std_gap1_FFs);
      disc_build_append_repeat(p_disc, 0x00, k_ibm_disc_std_sync_00s);
      for (i_sector = 0; i_sector < k_disc_ssd_sectors_per_track; ++i_sector) {
        /* Sector header, aka. ID. */
        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clocks(p_disc,
                                             k_ibm_disc_id_mark_data_pattern,
                                             k_ibm_disc_mark_clock_pattern);
        disc_build_append_single(p_disc, i_track);
        disc_build_append_single(p_disc, 0);
        disc_build_append_single(p_disc, i_sector);
        disc_build_append_single(p_disc, 1);
        disc_build_append_crc(p_disc);

        /* Sync pattern between sector header and sector data, aka. GAP 2. */
        disc_build_append_repeat(p_disc, 0xFF, k_ibm_disc_std_gap2_FFs);
        disc_build_append_repeat(p_disc, 0x00, k_ibm_disc_std_sync_00s);

        /* Sector data. */
        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clocks(p_disc,
                                             k_ibm_disc_data_mark_data_pattern,
                                             k_ibm_disc_mark_clock_pattern);
        disc_build_append_chunk(p_disc, p_ssd_data, k_disc_ssd_sector_size);
        disc_build_append_crc(p_disc);

        p_ssd_data += k_disc_ssd_sector_size;

        if (i_sector != (k_disc_ssd_sectors_per_track - 1)) {
          /* Sync pattern between sectors, aka. GAP 3. */
          disc_build_append_repeat(p_disc,
                                   0xFF,
                                   k_ibm_disc_std_10_sector_gap3_FFs);
          disc_build_append_repeat(p_disc, 0x00, k_ibm_disc_std_sync_00s);
        }
      } /* End of sectors loop. */

      /* Fill until end of track, aka. GAP 4. */
      disc_build_fill(p_disc, 0xFF);
    } /* End of side loop. */
  } /* End of track loop. */

  util_free(p_file_buf);
}
