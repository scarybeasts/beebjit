#include "disc.h"

#include "bbc_options.h"
#include "disc_fsd.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* This is 300RPM == 0.2s == 200000us per revolution, 3125 bytes per track,
   * 2 system ticks per us.
   * Or if you like, exactly 64us / 128 ticks.
   * We can get away without floating point here because it's exact. If we had
   * to support different rotational speeds or stretched / compressed disc
   * signal, we'd need to crack out the floating point.
   */
  k_disc_ticks_per_byte = (200000 / 3125 * 2),
  /* My Chinon drive holds the index pulse low for about 4ms, which is 62 bytes
   * worth of rotation.
   */
  k_disc_index_bytes = 62,
};

enum {
  k_disc_ssd_sector_size = 256,
  k_disc_ssd_sectors_per_track = 10,
  k_disc_ssd_tracks_per_disc = 80,
};

struct disc_track {
  uint8_t data[k_ibm_disc_bytes_per_track];
  uint8_t clocks[k_ibm_disc_bytes_per_track];
};

struct disc_side {
  struct disc_track tracks[k_ibm_disc_tracks_per_disc];
};

struct disc_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_byte_callback)(void*, uint8_t, uint8_t);
  void* p_byte_callback_object;
  void (*p_write_track_callback)(struct disc_struct*);

  int log_protection;

  intptr_t file_handle;
  uint8_t* p_format_metadata;
  int is_mutable;

  /* State of the disc. */
  struct disc_side lower_side;
  struct disc_side upper_side;
  int is_double_sided;
  uint32_t byte_position;
  int is_writeable;
  int is_track_dirty;

  /* State of the drive. */
  int is_side_upper;
  uint32_t track;

  uint16_t crc;
};

static void
disc_check_track_needs_write(struct disc_struct* p_disc) {
  if (!p_disc->is_track_dirty) {
    return;
  }
  if (!p_disc->is_mutable) {
    return;
  }

  p_disc->p_write_track_callback(p_disc);

  p_disc->is_track_dirty = 0;
}

static struct disc_side*
disc_get_side(struct disc_struct* p_disc) {
  if (p_disc->is_side_upper) {
    return &p_disc->upper_side;
  } else {
    return &p_disc->lower_side;
  }
}

static void
disc_timer_callback(struct disc_struct* p_disc) {
  struct disc_track* p_track;
  uint8_t data_byte;
  uint8_t clocks_byte;

  uint32_t byte_position = p_disc->byte_position;
  struct disc_side* p_side = disc_get_side(p_disc);

  if (p_disc->byte_position == 0) {
    disc_check_track_needs_write(p_disc);
  }

  p_track = &p_side->tracks[p_disc->track];
  data_byte = p_track->data[byte_position];
  clocks_byte = p_track->clocks[byte_position];

  (void) timing_set_timer_value(p_disc->p_timing,
                                p_disc->timer_id,
                                k_disc_ticks_per_byte);

  /* If there's an empty patch on the disc surface, the disc drive's head
   * amplifier will typically desperately seek for a signal in the noise,
   * resulting in "weak bits".
   * I've verified this with an oscilloscope on my Chinon F-051MD drive, which
   * has a Motorola MC3470AP head amplifier.
   * We need to return an inconsistent yet deterministic set of weak bits.
   */
  if ((data_byte == 0) && (clocks_byte == 0)) {
    uint64_t ticks = timing_get_total_timer_ticks(p_disc->p_timing);
    data_byte = ticks;
    data_byte ^= (ticks >> 8);
    data_byte ^= (ticks >> 16);
    data_byte ^= (ticks >> 24);
  }

  p_disc->p_byte_callback(p_disc->p_byte_callback_object,
                          data_byte,
                          clocks_byte);

  assert(byte_position < k_ibm_disc_bytes_per_track);
  byte_position++;
  if (byte_position == k_ibm_disc_bytes_per_track) {
    byte_position = 0;
  }
  p_disc->byte_position = byte_position;
}

struct disc_struct*
disc_create(struct timing_struct* p_timing,
            void (*p_byte_callback)(void* p, uint8_t data, uint8_t clock),
            void* p_byte_callback_object,
            struct bbc_options* p_options) {
  struct disc_struct* p_disc = malloc(sizeof(struct disc_struct));
  if (p_disc == NULL) {
    errx(1, "cannot allocate disc_struct");
  }

  (void) memset(p_disc, '\0', sizeof(struct disc_struct));

  p_disc->p_timing = p_timing;
  p_disc->p_byte_callback = p_byte_callback;
  p_disc->p_byte_callback_object = p_byte_callback_object;

  p_disc->log_protection = util_has_option(p_options->p_log_flags,
                                           "disc:protection");

  p_disc->file_handle = k_util_file_no_handle;

  p_disc->timer_id = timing_register_timer(p_timing,
                                           disc_timer_callback,
                                           p_disc);

  return p_disc;
}

void
disc_destroy(struct disc_struct* p_disc) {
  assert(!disc_is_spinning(p_disc));
  if (p_disc->p_format_metadata != NULL) {
    util_free(p_disc->p_format_metadata);
  }
  if (p_disc->file_handle != k_util_file_no_handle) {
    util_file_handle_close(p_disc->file_handle);
  }
  free(p_disc);
}

void
disc_build_track(struct disc_struct* p_disc,
                 int is_side_upper,
                 uint32_t track) {
  assert(!disc_is_spinning(p_disc));

  disc_select_side(p_disc, is_side_upper);
  disc_select_track(p_disc, track);
  p_disc->byte_position = 0;
}

static void
disc_put_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
  struct disc_side* p_side;
  struct disc_track* p_track;

  if (p_disc->is_side_upper) {
    p_side = &p_disc->upper_side;
  } else {
    p_side = &p_disc->lower_side;
  }

  p_track = &p_side->tracks[p_disc->track];

  p_track->data[p_disc->byte_position] = data;
  p_track->clocks[p_disc->byte_position] = clocks;
}

void
disc_write_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
  disc_put_byte(p_disc, data, clocks);
  p_disc->is_track_dirty = 1;
}

void
disc_build_reset_crc(struct disc_struct* p_disc) {
  p_disc->crc = ibm_disc_format_crc_init();
}

void
disc_build_append_single_with_clocks(struct disc_struct* p_disc,
                                     uint8_t data,
                                     uint8_t clocks) {
  disc_put_byte(p_disc, data, clocks);
  p_disc->crc = ibm_disc_format_crc_add_byte(p_disc->crc, data);
  p_disc->byte_position++;
}

void
disc_build_append_single(struct disc_struct* p_disc, uint8_t data) {
  disc_build_append_single_with_clocks(p_disc, data, 0xFF);
}

void
disc_build_append_repeat(struct disc_struct* p_disc,
                         uint8_t data,
                         size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_single(p_disc, data);
  }
}

void
disc_build_append_repeat_with_clocks(struct disc_struct* p_disc,
                                     uint8_t data,
                                     uint8_t clocks,
                                     size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_single_with_clocks(p_disc, data, clocks);
  }
}

void
disc_build_append_chunk(struct disc_struct* p_disc,
                        uint8_t* p_src,
                        size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_single(p_disc, p_src[i]);
  }
}

void
disc_build_append_crc(struct disc_struct* p_disc) {
  /* Cache the crc because the calls below will corrupt it. */
  uint16_t crc = p_disc->crc;

  disc_build_append_single(p_disc, (crc >> 8));
  disc_build_append_single(p_disc, (crc & 0xFF));
}

void
disc_build_append_bad_crc(struct disc_struct* p_disc) {
  /* Cache the crc because the calls below will corrupt it. */
  uint16_t crc = 0xFFFF;
  if (p_disc->crc == 0xFFFF) {
    crc = 0xFFFE;
  }

  disc_build_append_single(p_disc, (crc >> 8));
  disc_build_append_single(p_disc, (crc & 0xFF));
}

static void
disc_save_ssd(struct disc_struct* p_disc) {
  uint8_t* p_track_src;
  uint32_t i_sector;
  uint64_t seek_pos;

  intptr_t file_handle = p_disc->file_handle;
  struct disc_side* p_side = disc_get_side(p_disc);
  uint32_t track_size = (k_disc_ssd_sector_size * k_disc_ssd_sectors_per_track);
  uint32_t track = p_disc->track;

  assert(p_disc->is_track_dirty);

  seek_pos = (track_size * track);
  if (p_disc->is_double_sided) {
    seek_pos *= 2;
  }
  if (p_disc->is_side_upper) {
    seek_pos += track_size;
  }
  util_file_handle_seek(file_handle, seek_pos);

  p_track_src = &p_side->tracks[track].data[0];
  /* Skip GAP1. */
  p_track_src += (k_ibm_disc_std_gap1_FFs + k_ibm_disc_std_sync_00s);

  for (i_sector = 0; i_sector < k_disc_ssd_sectors_per_track; ++i_sector) {
    /* Skip header, GAP2 and data marker. */
    p_track_src += 7;
    p_track_src += (k_ibm_disc_std_gap2_FFs + k_ibm_disc_std_sync_00s);
    p_track_src += 1;

    util_file_handle_write(file_handle, p_track_src, k_disc_ssd_sector_size);
    p_track_src += k_disc_ssd_sector_size;
    /* Skip checksum. */
    p_track_src += 2;

    /* Skip GAP3. */
    p_track_src += (k_ibm_disc_std_10_sector_gap3_FFs +
                    k_ibm_disc_std_sync_00s);
  }
}

static void
disc_load_ssd(struct disc_struct* p_disc, int is_dsd) {
  uint64_t file_size;
  size_t read_ret;
  uint8_t buf[(k_disc_ssd_sector_size *
               k_disc_ssd_sectors_per_track *
               k_disc_ssd_tracks_per_disc *
               2)];
  uint32_t i_side;
  uint32_t i_track;
  uint32_t i_sector;

  intptr_t file_handle = p_disc->file_handle;
  uint64_t max_size = sizeof(buf);
  uint8_t* p_ssd_data = buf;
  uint32_t num_sides = 2;

  assert(file_handle != k_util_file_no_handle);

  p_disc->p_write_track_callback = disc_save_ssd;
  p_disc->is_double_sided = is_dsd;

  (void) memset(buf, '\0', sizeof(buf));

  if (!is_dsd) {
    max_size /= 2;
    num_sides = 1;
  }
  file_size = util_file_handle_get_size(file_handle);
  if (file_size > max_size) {
    errx(1, "ssd/dsd file too large");
  }
  if ((file_size % k_disc_ssd_sector_size) != 0) {
    errx(1, "ssd/dsd file not a sector multiple");
  }

  read_ret = util_file_handle_read(file_handle, buf, file_size);
  if (read_ret != file_size) {
    errx(1, "ssd/dsd file short read");
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
      assert(p_disc->byte_position <= k_ibm_disc_bytes_per_track);
      disc_build_append_repeat(p_disc,
                               0xFF,
                               (k_ibm_disc_bytes_per_track -
                                p_disc->byte_position));
    } /* End of side loop. */
  } /* End of track loop. */
}

static void
disc_hfe_extract_data(uint8_t* p_data, uint8_t* p_clock, uint8_t* p_src) {
  uint8_t data = 0;
  uint8_t clock = 0;

  uint8_t b0 = p_src[0];
  uint8_t b1 = p_src[1];
  uint8_t b2 = p_src[2];
  uint8_t b3 = p_src[3];

  if (b0 & 0x08) data |= 0x80;
  if (b0 & 0x80) data |= 0x40;
  if (b1 & 0x08) data |= 0x20;
  if (b1 & 0x80) data |= 0x10;
  if (b2 & 0x08) data |= 0x08;
  if (b2 & 0x80) data |= 0x04;
  if (b3 & 0x08) data |= 0x02;
  if (b3 & 0x80) data |= 0x01;
  if (b0 & 0x02) clock |= 0x80;
  if (b0 & 0x20) clock |= 0x40;
  if (b1 & 0x02) clock |= 0x20;
  if (b1 & 0x20) clock |= 0x10;
  if (b2 & 0x02) clock |= 0x08;
  if (b2 & 0x20) clock |= 0x04;
  if (b3 & 0x02) clock |= 0x02;
  if (b3 & 0x20) clock |= 0x01;


  *p_data = data;
  *p_clock = clock;
}

static void
disc_hfe_encode_data(uint8_t* p_dest, uint8_t data, uint8_t clock) {
  uint8_t b0 = 0;
  uint8_t b1 = 0;
  uint8_t b2 = 0;
  uint8_t b3 = 0;

  if (data & 0x80) b0 |= 0x08;
  if (data & 0x40) b0 |= 0x80;
  if (data & 0x20) b1 |= 0x08;
  if (data & 0x10) b1 |= 0x80;
  if (data & 0x08) b2 |= 0x08;
  if (data & 0x04) b2 |= 0x80;
  if (data & 0x02) b3 |= 0x08;
  if (data & 0x01) b3 |= 0x80;
  if (clock & 0x80) b0 |= 0x02;
  if (clock & 0x40) b0 |= 0x20;
  if (clock & 0x20) b1 |= 0x02;
  if (clock & 0x10) b1 |= 0x20;
  if (clock & 0x08) b2 |= 0x02;
  if (clock & 0x04) b2 |= 0x20;
  if (clock & 0x02) b3 |= 0x02;
  if (clock & 0x01) b3 |= 0x20;

  p_dest[0] = b0;
  p_dest[1] = b1;
  p_dest[2] = b2;
  p_dest[3] = b3;
}

static void
disc_hfe_write_track(struct disc_struct* p_disc) {
  uint32_t hfe_track_offset;
  uint32_t hfe_track_len;
  uint8_t* p_metadata;
  uint32_t write_len;
  uint32_t i_byte;
  uint8_t* p_src_data;
  uint8_t* p_src_clocks;
  uint8_t buf[64 * 4];

  intptr_t file_handle = p_disc->file_handle;
  uint32_t track = p_disc->track;

  assert(p_disc->is_track_dirty);

  p_metadata = p_disc->p_format_metadata;
  p_metadata += (track * 4);
  hfe_track_offset = (p_metadata[0] + (p_metadata[1] << 8));
  hfe_track_offset *= 512;
  if (p_disc->is_side_upper) {
    hfe_track_offset += 256;
  }
  hfe_track_len = (p_metadata[2] + (p_metadata[3] << 8));
  hfe_track_len /= 4;

  if (p_disc->is_side_upper) {
    p_src_data = &p_disc->upper_side.tracks[track].data[0];
    p_src_clocks = &p_disc->upper_side.tracks[track].clocks[0];
  } else {
    p_src_data = &p_disc->lower_side.tracks[track].data[0];
    p_src_clocks = &p_disc->lower_side.tracks[track].clocks[0];
  }

  write_len = k_ibm_disc_bytes_per_track;
  if (write_len > hfe_track_len) {
    write_len = hfe_track_len;
  }

  for (i_byte = 0; i_byte < write_len; ++i_byte) {
    uint32_t index = (i_byte % 64);

    disc_hfe_encode_data((buf + (index * 4)),
                         p_src_data[i_byte],
                         p_src_clocks[i_byte]);

    if ((index == 63) || (i_byte == (write_len - 1))) {
      util_file_handle_seek(file_handle, hfe_track_offset);
      util_file_handle_write(file_handle, buf, ((index + 1) * 4));
      hfe_track_offset += 512;
    }
  }
}

static void
disc_load_hfe(struct disc_struct* p_disc) {
  /* HFE (v1?):
   * https://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf
   */
  static const size_t k_max_hfe_size = (1024 * 1024 * 4);
  uint8_t buf[k_max_hfe_size];
  uint32_t file_len;
  uint32_t hfe_tracks;
  uint32_t i_track;
  uint32_t lut_offset;
  uint8_t* p_lut;

  intptr_t file_handle = p_disc->file_handle;
  int is_double_sided = 0;

  assert(file_handle != k_util_file_no_handle);

  p_disc->p_write_track_callback = disc_hfe_write_track;

  (void) memset(buf, '\0', sizeof(buf));

  p_disc->p_format_metadata = util_mallocz(512);

  file_len = util_file_handle_read(file_handle, buf, k_max_hfe_size);

  if (file_len == k_max_hfe_size) {
    errx(1, "hfe file too large");
  }

  if (file_len < 512) {
    errx(1, "hfe file no header");
  }
  if (memcmp(buf, "HXCPICFE", 8) != 0) {
    errx(1, "hfe file incorrect header");
  }
  if (buf[8] != '\0') {
    errx(1, "hfe file revision not 0");
  }
  if (buf[11] != 2) {
    errx(1, "hfe encoding not ISOIBM_FM_ENCODING");
  }
  if (buf[10] == 1) {
    is_double_sided = 0;
  } else if (buf[10] == 2) {
    is_double_sided = 1;
  } else {
    errx(1, "hfe invalid number of sides");
  }
  p_disc->is_double_sided = is_double_sided;

  hfe_tracks = buf[9];
  if (hfe_tracks > k_ibm_disc_tracks_per_disc) {
    errx(1, "hfe excessive tracks");
  }

  lut_offset = (buf[18] + (buf[19] << 8));
  lut_offset *= 512;

  if ((lut_offset + 512) > file_len) {
    errx(1, "hfe LUT doesn't fit");
  }

  (void) memcpy(p_disc->p_format_metadata, (buf + lut_offset), 512);
  p_lut = p_disc->p_format_metadata;

  for (i_track = 0; i_track < hfe_tracks; ++i_track) {
    uint32_t track_offset;
    uint32_t hfe_track_len;
    uint32_t read_track_bytes;
    uint8_t* p_track_lut;
    uint8_t* p_track_data;
    uint32_t i_byte;

    p_track_lut = (p_lut + (i_track * 4));
    track_offset = (p_track_lut[0] + (p_track_lut[1] << 8));
    track_offset *= 512;
    hfe_track_len = (p_track_lut[2] + (p_track_lut[3] << 8));

    if ((track_offset + hfe_track_len) > file_len) {
      errx(1, "hfe track doesn't fit");
    }

    /* Divide by 4; 2x sides and 2x FM bytes per data byte. */
    hfe_track_len /= 4;

    p_track_data = (buf + track_offset);
    read_track_bytes = k_ibm_disc_bytes_per_track;
    if (read_track_bytes > hfe_track_len) {
      read_track_bytes = hfe_track_len;
    }

    for (i_byte = 0; i_byte < read_track_bytes; ++i_byte) {
      uint8_t data;
      uint8_t clock;
      uint32_t index = (i_byte / 64);
      index *= 512;
      index += ((i_byte % 64) * 4);

      disc_hfe_extract_data(&data, &clock, (p_track_data + index));
      p_disc->lower_side.tracks[i_track].data[i_byte] = data;
      p_disc->lower_side.tracks[i_track].clocks[i_byte] = clock;
      if (is_double_sided) {
        disc_hfe_extract_data(&data, &clock, (p_track_data + index + 256));
        p_disc->upper_side.tracks[i_track].data[i_byte] = data;
        p_disc->upper_side.tracks[i_track].clocks[i_byte] = clock;
      }
    }
  }
}

static void
disc_convert_to_hfe(struct disc_struct* p_disc) {
  uint32_t i_track;
  uint8_t header[512];
  uint8_t* p_metadata;

  intptr_t file_handle = p_disc->file_handle;
  uint32_t hfe_track_len = (k_ibm_disc_bytes_per_track * 8);
  uint32_t hfe_offset = 2;
  uint32_t hfe_offset_delta = ((hfe_track_len / 512) + 1);

  p_disc->p_write_track_callback = disc_hfe_write_track;

  /* Fill with 0xFF; that is what the command line HFE tools do, and also, 0xFF
   * appears to be the byte used for the default / sane boolean option.
   */
  (void) memset(header, '\xFF', sizeof(header));
  (void) strcpy((char*) header, "HXCPICFE");
  /* Revision 0. */
  header[8] = 0;
  /* 80 tracks, sides as appropriate. */
  header[9] = 80;
  if (p_disc->is_double_sided) {
    header[10] = 2;
  } else {
    header[10] = 1;
  }
  /* IBM FM, 250kbit, (unused) RPM. */
  header[11] = 2;
  header[12] = 0xFA;
  header[13] = 0;
  header[14] = 0;
  header[15] = 0;
  /* Mode: Shuggart DD. Unused. 1==512 LUT offset. */
  header[16] = 7;
  header[17] = 0xFF;
  header[18] = 1;
  header[19] = 0;
  /* Write allowed, single step, no alternate track options. */
  header[20] = 0xFF;
  header[21] = 0xFF;
  header[22] = 0xFF;
  header[23] = 0xFF;
  header[24] = 0xFF;
  header[25] = 0xFF;

  util_file_handle_seek(file_handle, 0);
  util_file_handle_write(file_handle, header, 512);

  assert(p_disc->p_format_metadata == NULL);
  p_metadata = util_mallocz(512);
  p_disc->p_format_metadata = p_metadata;

  for (i_track = 0; i_track < k_ibm_disc_tracks_per_disc; ++i_track) {
    uint32_t index = (i_track * 4);
    p_metadata[index] = (hfe_offset & 0xFF);
    p_metadata[index + 1] = (hfe_offset >> 8);
    p_metadata[index + 2] = (hfe_track_len & 0xFF);
    p_metadata[index + 3] = (hfe_track_len >> 8);

    p_disc->track = i_track;
    p_disc->is_side_upper = 0;
    p_disc->is_track_dirty = 1;
    disc_hfe_write_track(p_disc);
    if (p_disc->is_double_sided) {
      p_disc->is_side_upper = 1;
      p_disc->is_track_dirty = 1;
      disc_hfe_write_track(p_disc);
    }

    hfe_offset += hfe_offset_delta;
  }

  p_disc->is_track_dirty = 0;

  util_file_handle_seek(file_handle, 512);
  util_file_handle_write(file_handle, p_metadata, 512);
}

void
disc_load(struct disc_struct* p_disc,
          const char* p_file_name,
          int is_writeable,
          int is_mutable) {
  int is_file_writeable = 0;

  if (is_mutable) {
    is_file_writeable = 1;
  }
  p_disc->file_handle = util_file_handle_open(p_file_name,
                                              is_file_writeable,
                                              0);

  if (util_is_extension(p_file_name, "ssd")) {
    disc_load_ssd(p_disc, 0);
  } else if (util_is_extension(p_file_name, "dsd")) {
    disc_load_ssd(p_disc, 1);
  } else if (util_is_extension(p_file_name, "fsd")) {
    disc_fsd_load(p_disc, p_disc->file_handle, p_disc->log_protection);
  } else if (util_is_extension(p_file_name, "hfe")) {
    disc_load_hfe(p_disc);
  } else {
    errx(1, "unknown disc filename extension");
  }

  if (is_mutable && (p_disc->p_write_track_callback == NULL)) {
    char new_file_name[256];
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.hfe",
                    p_file_name);
    log_do_log(k_log_disc,
               k_log_info,
               "cannot writeback to file type, converting to HFE: %s",
               new_file_name);
    util_file_handle_close(p_disc->file_handle);
    p_disc->file_handle = util_file_handle_open(new_file_name, 1, 1);
    disc_convert_to_hfe(p_disc);
  }

  p_disc->is_side_upper = 0;
  p_disc->track = 0;
  p_disc->byte_position = 0;

  p_disc->is_writeable = is_writeable;
  p_disc->is_mutable = is_mutable;
}

int
disc_is_write_protected(struct disc_struct* p_disc) {
  return !p_disc->is_writeable;
}

uint32_t
disc_get_track(struct disc_struct* p_disc) {
  return p_disc->track;
}

int
disc_is_index_pulse(struct disc_struct* p_disc) {
  /* EMU: the 8271 datasheet says that the index pulse must be held for over
   * 0.5us.
   */
  if (p_disc->byte_position < k_disc_index_bytes) {
    return 1;
  }
  return 0;
}

uint32_t
disc_get_head_position(struct disc_struct* p_disc) {
  return p_disc->byte_position;
}

int
disc_is_spinning(struct disc_struct* p_disc) {
  return timing_timer_is_running(p_disc->p_timing, p_disc->timer_id);
}

void
disc_start_spinning(struct disc_struct* p_disc) {
  (void) timing_start_timer_with_value(p_disc->p_timing,
                                       p_disc->timer_id,
                                       k_disc_ticks_per_byte);
}

void
disc_stop_spinning(struct disc_struct* p_disc) {
  disc_check_track_needs_write(p_disc);
  (void) timing_stop_timer(p_disc->p_timing, p_disc->timer_id);
}

void
disc_select_side(struct disc_struct* p_disc, int side) {
  disc_check_track_needs_write(p_disc);

  p_disc->is_side_upper = side;
}

void
disc_select_track(struct disc_struct* p_disc, uint32_t track) {
  disc_check_track_needs_write(p_disc);

  if (track >= k_ibm_disc_tracks_per_disc) {
    track = (k_ibm_disc_tracks_per_disc - 1);
  }
  p_disc->track = track;
}

void
disc_seek_track(struct disc_struct* p_disc, int32_t delta) {
  int32_t new_track = ((int32_t) p_disc->track + delta);
  if (new_track < 0) {
    new_track = 0;
  }
  disc_select_track(p_disc, new_track);
}
