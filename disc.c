#include "disc.h"

#include "bbc_options.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_disc_bytes_per_track = 3125,
  k_disc_tracks_per_disc = 80,
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
  uint8_t data[k_disc_bytes_per_track];
  uint8_t clocks[k_disc_bytes_per_track];
};

struct disc_side {
  struct disc_track tracks[k_disc_tracks_per_disc];
};

struct disc_struct {
  struct timing_struct* p_timing;
  uint32_t timer_id;

  void (*p_byte_callback)(void*, uint8_t, uint8_t);
  void* p_byte_callback_object;

  int log_protection;

  intptr_t file_handle;
  int is_mutable;

  /* State of the disc. */
  struct disc_side lower_side;
  struct disc_side upper_side;
  uint32_t byte_position;
  int is_writeable;

  /* State of the drive. */
  int is_side_upper;
  uint32_t track;

  uint16_t crc;
};

static void
disc_timer_callback(struct disc_struct* p_disc) {
  struct disc_side* p_side;
  struct disc_track* p_track;
  uint8_t data_byte;
  uint8_t clocks_byte;

  if (p_disc->is_side_upper) {
    p_side = &p_disc->upper_side;
  } else {
    p_side = &p_disc->lower_side;
  }

  p_track = &p_side->tracks[p_disc->track];
  data_byte = p_track->data[p_disc->byte_position];
  clocks_byte = p_track->clocks[p_disc->byte_position];

  (void) timing_set_timer_value(p_disc->p_timing,
                                p_disc->timer_id,
                                k_disc_ticks_per_byte);

  p_disc->p_byte_callback(p_disc->p_byte_callback_object,
                          data_byte,
                          clocks_byte);

  assert(p_disc->byte_position < k_disc_bytes_per_track);
  p_disc->byte_position++;
  if (p_disc->byte_position == k_disc_bytes_per_track) {
    p_disc->byte_position = 0;
  }
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
  if (p_disc->file_handle != k_util_file_no_handle) {
    util_file_handle_close(p_disc->file_handle);
  }
  free(p_disc);
}

static void
disc_build_track(struct disc_struct* p_disc,
                 int is_side_upper,
                 uint32_t track) {
  assert(!disc_is_spinning(p_disc));

  disc_select_side(p_disc, is_side_upper);
  disc_select_track(p_disc, track);
  p_disc->byte_position = 0;
}

void
disc_write_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
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

static void
disc_build_reset_crc(struct disc_struct* p_disc) {
  p_disc->crc = ibm_disc_format_crc_init();
}

static void
disc_build_append_single_with_clock(struct disc_struct* p_disc,
                                    uint8_t data,
                                    uint8_t clocks) {
  disc_write_byte(p_disc, data, clocks);
  p_disc->crc = ibm_disc_format_crc_add_byte(p_disc->crc, data);
  p_disc->byte_position++;
}

static void
disc_build_append_single(struct disc_struct* p_disc, uint8_t data) {
  disc_build_append_single_with_clock(p_disc, data, 0xFF);
}

static void
disc_build_append_repeat(struct disc_struct* p_disc,
                         uint8_t data,
                         size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_single(p_disc, data);
  }
}

static void
disc_build_append_chunk(struct disc_struct* p_disc,
                        uint8_t* p_src,
                        size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_single(p_disc, p_src[i]);
  }
}

static void
disc_build_append_crc(struct disc_struct* p_disc) {
  /* Cache the crc because the calls below will corrupt it. */
  uint16_t crc = p_disc->crc;

  disc_build_append_single(p_disc, (crc >> 8));
  disc_build_append_single(p_disc, (crc & 0xFF));
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
      disc_build_append_repeat(p_disc, 0xFF, 16);
      disc_build_append_repeat(p_disc, 0x00, 6);
      for (i_sector = 0; i_sector < k_disc_ssd_sectors_per_track; ++i_sector) {
        /* Sector header, aka. ID. */
        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clock(p_disc,
                                            k_ibm_disc_id_mark_data_pattern,
                                            k_ibm_disc_mark_clock_pattern);
        disc_build_append_single(p_disc, i_track);
        disc_build_append_single(p_disc, 0);
        disc_build_append_single(p_disc, i_sector);
        disc_build_append_single(p_disc, 1);
        disc_build_append_crc(p_disc);

        /* Sync pattern between sector header and sector data, aka. GAP 2. */
        disc_build_append_repeat(p_disc, 0xFF, 11);
        disc_build_append_repeat(p_disc, 0x00, 6);

        /* Sector data. */
        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clock(p_disc,
                                            k_ibm_disc_data_mark_data_pattern,
                                            k_ibm_disc_mark_clock_pattern);
        disc_build_append_chunk(p_disc, p_ssd_data, k_disc_ssd_sector_size);
        disc_build_append_crc(p_disc);

        p_ssd_data += k_disc_ssd_sector_size;

        if (i_sector != (k_disc_ssd_sectors_per_track - 1)) {
          /* Sync pattern between sectors, aka. GAP 3. */
          disc_build_append_repeat(p_disc, 0xFF, 16);
          disc_build_append_repeat(p_disc, 0x00, 6);
        }
      } /* End of sectors loop. */
      /* Fill until end of track, aka. GAP 4. */
      assert(p_disc->byte_position <= k_disc_bytes_per_track);
      disc_build_append_repeat(p_disc,
                               0xFF,
                               (k_disc_bytes_per_track -
                                   p_disc->byte_position));
    } /* End of side loop. */
  } /* End of track loop. */
}

static void
disc_load_fsd(struct disc_struct* p_disc) {
  /* The most authoritative "documentation" for the FSD format appears to be:
   * https://stardot.org.uk/forums/viewtopic.php?f=4&t=4353&start=30#p66240
   */
  static const size_t k_max_fsd_size = (1024 * 1024);
  uint8_t buf[k_max_fsd_size];
  size_t len;
  size_t file_remaining;
  uint8_t* p_buf;
  uint32_t fsd_tracks;
  uint32_t i_track;
  uint32_t real_sector_size;
  uint8_t logical_track;
  uint8_t logical_sector;
  uint8_t sector_error;
  uint8_t title_char;
  int do_read_data;

  (void) memset(buf, '\0', k_max_fsd_size);

  len = util_file_handle_read(p_disc->file_handle, buf, k_max_fsd_size);

  if (len == k_max_fsd_size) {
    errx(1, "fsd file too large");
  }

  p_buf = buf;
  file_remaining = len;
  if (file_remaining < 8) {
    errx(1, "fsd file no header");
  }
  if (memcmp(p_buf, "FSD", 3) != 0) {
    errx(1, "fsd file incorrect header");
  }
  p_buf += 8;
  file_remaining -= 8;
  do {
    if (file_remaining == 0) {
      errx(1, "fsd file missing title");
    }
    title_char = *p_buf;
    p_buf++;
    file_remaining--;
  } while (title_char != 0);

  if (file_remaining == 0) {
    errx(1, "fsd file missing tracks");
  }
  /* This appears to actually be "max zero-indexed track ID" so we add 1. */
  fsd_tracks = *p_buf;
  fsd_tracks++;
  p_buf++;
  file_remaining--;
  if (fsd_tracks > k_disc_tracks_per_disc) {
    errx(1, "fsd file too many tracks: %d", fsd_tracks);
  }

  for (i_track = 0; i_track < fsd_tracks; ++i_track) {
    uint32_t fsd_sectors;
    uint32_t i_sector;

    uint32_t track_remaining = k_disc_bytes_per_track;
    /* Acorn format command standard for 256 byte sectors. The 8271 datasheet
     * suggests 21.
     */
    uint32_t gap3_ff_count = 16;

    if (file_remaining < 2) {
      errx(1, "fsd file missing track header");
    }
    if (p_buf[0] != i_track) {
      errx(1, "fsd file unmatched track id");
    }

    disc_build_track(p_disc, 0, i_track);

    fsd_sectors = p_buf[1];
    p_buf += 2;
    file_remaining -= 2;
    if (fsd_sectors == 0) {
      if (p_disc->log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: unformatted track %d",
                   i_track);
      }
      disc_build_append_repeat(p_disc, 0, k_disc_bytes_per_track);
      continue;
    } else if ((fsd_sectors != 10) && p_disc->log_protection) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "FSD: non-standard sector count track %d count %d",
                 i_track,
                 fsd_sectors);
    }
    if (fsd_sectors > 10) {
      /* Standard for 128 byte sectors. If we didn't lower the value here, the
       * track wouldn't fit.
       */
      gap3_ff_count = 11;
    }
    if (file_remaining == 0) {
      errx(1, "fsd file missing readable flag");
    }

    if (*p_buf == 0) {
      /* "unreadable" track. */
      if (p_disc->log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: unreadable track %d",
                   i_track);
      }
      do_read_data = 0;
    } else if (*p_buf == 0xFF) {
      do_read_data = 1;
    } else {
      errx(1, "fsd file unknown readable byte value");
    }
    p_buf++;
    file_remaining--;

    /* Sync pattern at start of track, as the index pulse starts, aka GAP 5. */
    disc_build_append_repeat(p_disc, 0xFF, 16);
    disc_build_append_repeat(p_disc, 0x00, 6);
    track_remaining -= 22;

    for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
      if (file_remaining < 4) {
        errx(1, "fsd file missing sector header");
      }
      if (track_remaining < (7 + 17)) {
        errx(1, "fsd file track no space for sector header and gap");
      }
      /* Sector header, aka. ID. */
      disc_build_reset_crc(p_disc);
      disc_build_append_single_with_clock(p_disc,
                                          k_ibm_disc_id_mark_data_pattern,
                                          k_ibm_disc_mark_clock_pattern);
      logical_track = p_buf[0];
      logical_sector = p_buf[2];
      if ((logical_track != i_track) && p_disc->log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: track mismatch physical %d logical %d sector %d",
                   i_track,
                   logical_track,
                   logical_sector);
      }
      disc_build_append_single(p_disc, logical_track);
      disc_build_append_single(p_disc, p_buf[1]);
      disc_build_append_single(p_disc, logical_sector);
      disc_build_append_single(p_disc, p_buf[3]);
      disc_build_append_crc(p_disc);

      /* Sync pattern between sector header and sector data, aka. GAP 2. */
      disc_build_append_repeat(p_disc, 0xFF, 11);
      disc_build_append_repeat(p_disc, 0x00, 6);
      p_buf += 4;
      file_remaining -= 4;
      track_remaining -= (7 + 17);

      if (do_read_data) {
        uint8_t sector_mark = k_ibm_disc_data_mark_data_pattern;
        int crc_error = 0;

        if (file_remaining < 2) {
          errx(1, "fsd file missing sector header second part");
        }

        real_sector_size = p_buf[0];
        if (real_sector_size > 4) {
          errx(1, "fsd file excessive sector size");
        }
        sector_error = p_buf[1];
        if (sector_error == 0x20) {
          /* Deleted data. */
          if (p_disc->log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: deleted sector track %d logical sector %d",
                       i_track,
                       logical_sector);
          }
          sector_mark = k_ibm_disc_deleted_data_mark_data_pattern;
        } else if (sector_error == 0x0E) {
          /* Sector has data CRC error. */
          if (p_disc->log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: CRC error sector track %d logical sector %d",
                       i_track,
                       logical_sector);
          }
          crc_error = 1;
        } else if (sector_error != 0) {
          errx(1, "fsd file sector error %d unsupported", sector_error);
        }
        p_buf += 2;
        file_remaining -= 2;

        real_sector_size = (1 << (7 + real_sector_size));
        if (file_remaining < real_sector_size) {
          errx(1, "fsd file missing sector data");
        }
        if (track_remaining < (real_sector_size + 3)) {
          errx(1, "fsd file track no space for sector data");
        }

        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clock(p_disc,
                                            sector_mark,
                                            k_ibm_disc_mark_clock_pattern);
        disc_build_append_chunk(p_disc, p_buf, real_sector_size);
        if (crc_error) {
          /* CRC error required. Use 0xFFFF unless that's accidentally correct,
           * in which case 0xFFFE.
           */
          if (p_disc->crc == 0xFFFF) {
            p_disc->crc = 0xFFFE;
          } else {
            p_disc->crc = 0xFFFF;
          }
        }
        disc_build_append_crc(p_disc);

        p_buf += real_sector_size;
        file_remaining -= real_sector_size;
        track_remaining -= (real_sector_size + 3);
      }

      if (i_sector != (fsd_sectors - 1)) {
        /* Sync pattern between sectors, aka. GAP 3. */
        if (track_remaining < 22) {
          errx(1, "fsd file track no space for inter sector gap");
        }
        disc_build_append_repeat(p_disc, 0xFF, gap3_ff_count);
        disc_build_append_repeat(p_disc, 0x00, 6);
        track_remaining -= (gap3_ff_count + 6);
      }
    } /* End of sectors loop. */

    /* Fill until end of track, aka. GAP 4. */
    assert(p_disc->byte_position <= k_disc_bytes_per_track);
    disc_build_append_repeat(p_disc,
                             0xFF,
                             (k_disc_bytes_per_track - p_disc->byte_position));
  } /* End of track loop. */
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
    disc_load_fsd(p_disc);
  } else {
    errx(1, "unknown disc filename extension");
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
  (void) timing_stop_timer(p_disc->p_timing, p_disc->timer_id);
}

void
disc_select_side(struct disc_struct* p_disc, int side) {
  p_disc->is_side_upper = side;
}

void
disc_select_track(struct disc_struct* p_disc, uint32_t track) {
  if (track >= k_disc_tracks_per_disc) {
    track = (k_disc_tracks_per_disc - 1);
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
