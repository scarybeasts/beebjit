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
disc_ssd_write_track(struct disc_struct* p_disc) {
  uint8_t* p_track_src;
  uint32_t i_sector;
  uint64_t seek_pos;

  struct util_file* p_file = disc_get_file(p_disc);
  uint32_t track_size = (k_disc_ssd_sector_size * k_disc_ssd_sectors_per_track);
  uint32_t track = disc_get_track(p_disc);

  assert(disc_is_track_dirty(p_disc));

  seek_pos = (track_size * track);
  if (disc_is_double_sided(p_disc)) {
    seek_pos *= 2;
  }
  if (disc_is_upper_side(p_disc)) {
    seek_pos += track_size;
  }
  util_file_seek(p_file, seek_pos);

  p_track_src = disc_get_raw_track_data(p_disc);
  /* Skip GAP1. */
  p_track_src += (k_ibm_disc_std_gap1_FFs + k_ibm_disc_std_sync_00s);

  for (i_sector = 0; i_sector < k_disc_ssd_sectors_per_track; ++i_sector) {
    /* Skip header, GAP2 and data marker. */
    p_track_src += 7;
    p_track_src += (k_ibm_disc_std_gap2_FFs + k_ibm_disc_std_sync_00s);
    p_track_src += 1;

    util_file_write(p_file, p_track_src, k_disc_ssd_sector_size);
    p_track_src += k_disc_ssd_sector_size;
    /* Skip checksum. */
    p_track_src += 2;

    /* Skip GAP3. */
    p_track_src += (k_ibm_disc_std_10_sector_gap3_FFs +
                    k_ibm_disc_std_sync_00s);
  }
}

void
disc_ssd_load(struct disc_struct* p_disc, int is_dsd) {
  uint64_t file_size;
  size_t read_ret;
  uint8_t buf[(k_disc_ssd_sector_size *
               k_disc_ssd_sectors_per_track *
               k_disc_ssd_tracks_per_disc *
               2)];
  uint32_t i_side;
  uint32_t i_track;
  uint32_t i_sector;

  struct util_file* p_file = disc_get_file(p_disc);
  uint64_t max_size = sizeof(buf);
  uint8_t* p_ssd_data = buf;
  uint32_t num_sides = 2;

  assert(p_file != NULL);

  disc_set_is_double_sided(p_disc, is_dsd);

  (void) memset(buf, '\0', sizeof(buf));

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

  read_ret = util_file_read(p_file, buf, file_size);
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
      assert(disc_get_head_position(p_disc) <= k_ibm_disc_bytes_per_track);
      disc_build_append_repeat(p_disc,
                               0xFF,
                               (k_ibm_disc_bytes_per_track -
                                disc_get_head_position(p_disc)));
    } /* End of side loop. */
  } /* End of track loop. */
}
