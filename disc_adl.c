#include "disc_adl.h"

#include "disc.h"
#include "disc_tool.h"
#include "ibm_disc_format.h"
#include "util.h"

#include <assert.h>
#include <string.h>

enum {
  k_disc_adl_sector_size = 256,
  k_disc_adl_sectors_per_track = 16,
  k_disc_adl_tracks_per_disc = 80,
};

void
disc_adl_write_track(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t pulses_length,
                     uint32_t* p_pulses) {
  uint64_t seek_pos;
  uint32_t num_sectors;
  uint32_t i_sectors;
  struct disc_tool_sector* p_sectors;

  struct disc_tool_struct* p_tool = disc_tool_create();
  struct util_file* p_file = disc_get_file(p_disc);
  uint32_t track_size = (k_disc_adl_sector_size * k_disc_adl_sectors_per_track);

  (void) pulses_length;
  (void) p_pulses;

  seek_pos = (track_size * track);
  seek_pos *= 2;
  if (is_side_upper) {
    seek_pos += track_size;
  }

  disc_tool_set_disc(p_tool, p_disc);
  disc_tool_set_is_side_upper(p_tool, is_side_upper);
  disc_tool_set_track(p_tool, track);
  disc_tool_find_sectors(p_tool);
  p_sectors = disc_tool_get_sectors(p_tool, &num_sectors);

  for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
    uint8_t sector_id;
    uint32_t sector_length;
    uint8_t sector_data[k_disc_tool_max_sector_length];
    struct disc_tool_sector* p_sector = &p_sectors[i_sectors];
    if (!p_sector->is_mfm) {
      continue;
    }
    if (p_sector->header_bytes[0] != track) {
      continue;
    }
    sector_id = p_sector->header_bytes[2];
    if (sector_id > 15) {
      continue;
    }
    disc_tool_read_sector(p_tool,
                          &sector_length,
                          &sector_data[0],
                          i_sectors,
                          0);
    if (sector_length != 256) {
      continue;
    }
    util_file_seek(p_file, (seek_pos + (sector_id * k_disc_adl_sector_size)));
    util_file_write(p_file, &sector_data[0], 256);
  }

  disc_tool_destroy(p_tool);
}

void
disc_adl_load(struct disc_struct* p_disc) {
  static const uint32_t k_max_adl_size = (k_disc_adl_sector_size *
                                          k_disc_adl_sectors_per_track *
                                          k_disc_adl_tracks_per_disc *
                                          2);
  uint64_t file_size;
  size_t read_ret;
  uint8_t* p_file_buf;
  uint32_t i_side;
  uint32_t i_track;
  uint32_t i_sector;

  struct util_file* p_file = disc_get_file(p_disc);
  uint8_t* p_adl_data;
  uint32_t num_sides = 2;
  uint32_t max_size = k_max_adl_size;

  assert(p_file != NULL);

  /* Must zero it out because it is all read even if the file is short. */
  p_file_buf = util_mallocz(k_max_adl_size);
  p_adl_data = p_file_buf;

  file_size = util_file_get_size(p_file);
  if (file_size > max_size) {
    util_bail("adl file too large");
  }
  if ((file_size % k_disc_adl_sector_size) != 0) {
    util_bail("adl file not a sector multiple");
  }

  read_ret = util_file_read(p_file, p_file_buf, file_size);
  if (read_ret != file_size) {
    util_bail("adl file short read");
  }

  for (i_track = 0; i_track < k_disc_adl_tracks_per_disc; ++i_track) {
    for (i_side = 0; i_side < num_sides; ++i_side) {
      /* Using recommended values from the 177x datasheet. */
      disc_build_track(p_disc, i_side, i_track);
      disc_build_append_repeat_mfm_byte(p_disc, 0x4E, 60);
      for (i_sector = 0; i_sector < k_disc_adl_sectors_per_track; ++i_sector) {
        disc_build_append_repeat_mfm_byte(p_disc, 0x00, 12);
        disc_build_reset_crc(p_disc);
        disc_build_append_mfm_3x_A1_sync(p_disc);
        disc_build_append_mfm_byte(p_disc, k_ibm_disc_id_mark_data_pattern);
        disc_build_append_mfm_byte(p_disc, i_track);
        disc_build_append_mfm_byte(p_disc, 0);
        disc_build_append_mfm_byte(p_disc, i_sector);
        disc_build_append_mfm_byte(p_disc, 1);
        disc_build_append_crc(p_disc, 1);

        /* Sync pattern between sector header and sector data, aka. GAP 2. */
        disc_build_append_repeat_mfm_byte(p_disc, 0x4E, 22);
        disc_build_append_repeat_mfm_byte(p_disc, 0x00, 12);

        /* Sector data. */
        disc_build_reset_crc(p_disc);
        disc_build_append_mfm_3x_A1_sync(p_disc);
        disc_build_append_mfm_byte(p_disc, k_ibm_disc_data_mark_data_pattern);
        disc_build_append_mfm_chunk(p_disc, p_adl_data, k_disc_adl_sector_size);
        disc_build_append_crc(p_disc, 1);

        p_adl_data += k_disc_adl_sector_size;

        /* Sync pattern between sectors, aka. GAP 3. */
        disc_build_append_repeat_mfm_byte(p_disc, 0x4E, 24);
      } /* End of sectors loop. */

      /* Fill until end of track, aka. GAP 4. */
      disc_build_fill_mfm_byte(p_disc, 0x4E);
    } /* End of side loop. */
  } /* End of track loop. */

  util_free(p_file_buf);
}
