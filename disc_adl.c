#include "disc_adl.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "util.h"

#include <assert.h>
#include <string.h>

enum {
  k_disc_adl_sector_size = 256,
  k_disc_adl_sectors_per_track = 16,
  k_disc_adl_tracks_per_disc = 80,
};

static uint16_t
disc_adl_get_mfm_pulses(uint32_t* p_pulses, uint32_t index) {
  uint32_t pulses;
  uint16_t mfm_pulses;

  assert(index < (k_ibm_disc_bytes_per_track * 2));

  pulses = p_pulses[index / 2];
  if (index & 1) {
    mfm_pulses = (pulses & 0xFFFF);
  } else {
    mfm_pulses = (pulses >> 16);
  }

  return mfm_pulses;
}

void
disc_adl_write_track(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t pulses_length,
                     uint32_t* p_pulses) {
  uint32_t i;
  uint64_t seek_pos;
  uint32_t a1_count = 0;
  int32_t sector = -1;

  struct util_file* p_file = disc_get_file(p_disc);
  uint32_t track_size = (k_disc_adl_sector_size * k_disc_adl_sectors_per_track);
  uint32_t byte_length = (pulses_length * 2);

  assert(pulses_length == k_ibm_disc_bytes_per_track);

  seek_pos = (track_size * track);
  seek_pos *= 2;
  if (is_side_upper) {
    seek_pos += track_size;
  }

  a1_count = 0;
  for (i = 0; i < byte_length; ++i) {
    uint32_t j;
    uint8_t data;
    uint16_t mfm_pulses;

    if ((i + 1 + k_disc_adl_sector_size) > byte_length) {
      break;
    }

    mfm_pulses = disc_adl_get_mfm_pulses(p_pulses, i);
    data = ibm_disc_format_2us_pulses_to_mfm(mfm_pulses);

    if (mfm_pulses == k_ibm_disc_mfm_a1_sync) {
      a1_count++;
      continue;
    }
    if (a1_count != 3) {
      a1_count = 0;
      continue;
    }
    a1_count = 0;

    if (data == k_ibm_disc_id_mark_data_pattern) {
      mfm_pulses = disc_adl_get_mfm_pulses(p_pulses, (i + 3));
      sector = ibm_disc_format_2us_pulses_to_mfm(mfm_pulses);
      continue;
    }
    if (data != k_ibm_disc_data_mark_data_pattern) {
      continue;
    }
    if ((sector < 0) || (sector > 15)) {
      continue;
    }

    util_file_seek(p_file, (seek_pos + (sector * k_disc_adl_sector_size)));
    i += 1;
    for (j = 0; j < k_disc_adl_sector_size; ++j) {
      mfm_pulses = disc_adl_get_mfm_pulses(p_pulses, i);
      data = ibm_disc_format_2us_pulses_to_mfm(mfm_pulses);
      util_file_write(p_file, &data, 1);
      ++i;
    }
  }
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
