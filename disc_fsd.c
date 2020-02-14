#include "disc_fsd.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t
disc_fsd_calculate_track_total_bytes(uint32_t fsd_sectors,
                                     uint32_t track_data_bytes,
                                     uint32_t gap1_ff_count,
                                     uint32_t gap2_ff_count,
                                     uint32_t gap3_ff_count) {
  uint32_t ret = track_data_bytes;
  /* Each sector's data has a marker and CRC. */
  ret += (fsd_sectors * 3);
  /* Each sector has a sector header with marker and CRC. */
  ret += (fsd_sectors * 7);
  /* Each sector has GAP2, between the header and data. */
  ret += (fsd_sectors * (gap2_ff_count + 6));
  /* Each sector, bar the last, has GAP3, the inter-sector gap. */
  ret += ((fsd_sectors - 1) * (gap3_ff_count + 6));
  /* Before the first sector is GAP1. */
  ret += (gap1_ff_count + 6);

  return ret;
}

void
disc_fsd_load(struct disc_struct* p_disc,
              intptr_t file_handle,
              int log_protection) {
  /* The most authoritative "documentation" for the FSD format appears to be:
   * https://stardot.org.uk/forums/viewtopic.php?f=4&t=4353&start=60#p195518
   */
  static const size_t k_max_fsd_size = (1024 * 1024);
  uint8_t buf[k_max_fsd_size];
  size_t len;
  size_t file_remaining;
  uint8_t* p_buf;
  uint32_t fsd_tracks;
  uint32_t i_track;
  uint8_t title_char;
  int do_read_data;

  assert(file_handle != k_util_file_no_handle);

  (void) memset(buf, '\0', sizeof(buf));

  len = util_file_handle_read(file_handle, buf, sizeof(buf));

  if (len == sizeof(buf)) {
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
  if (fsd_tracks > k_ibm_disc_tracks_per_disc) {
    errx(1, "fsd file too many tracks: %d", fsd_tracks);
  }

  for (i_track = 0; i_track < fsd_tracks; ++i_track) {
    uint32_t fsd_sectors;
    uint32_t i_sector;
    size_t saved_file_remaining;
    uint8_t* p_saved_buf;
    uint8_t sector_seen[256];
    uint32_t track_total_bytes;

    uint32_t track_remaining = k_ibm_disc_bytes_per_track;
    uint32_t track_data_bytes = 0;
    /* Acorn format command standards for 256 byte sectors. The 8271 datasheet
     * generally agrees but does suggest 21 for GAP3.
     */
    uint32_t gap1_ff_count = 16;
    uint32_t gap2_ff_count = 11;
    uint32_t gap3_ff_count = 16;
    int do_data_truncate = 0;
    uint32_t num_multi_size_sectors = 0;
    uint32_t num_bytes_over = 0;

    (void) memset(sector_seen, '\0', sizeof(sector_seen));

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
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: unformatted track %d",
                   i_track);
      }
      disc_build_append_repeat(p_disc, 0, k_ibm_disc_bytes_per_track);
      continue;
    } else if ((fsd_sectors != 10) && log_protection) {
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
      if (log_protection) {
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

    /* Pass 1: find total data bytes. */
    saved_file_remaining = file_remaining;
    p_saved_buf = p_buf;
    if (do_read_data) {
      for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
        uint32_t real_sector_size;
        uint8_t sector_error;

        if (file_remaining < 6) {
          errx(1, "fsd file missing sector header");
        }
        real_sector_size = p_buf[4];
        sector_error = p_buf[5];
        if ((sector_error >= 0xE0) && (sector_error <= 0xE2)) {
          num_multi_size_sectors++;
        }
        if (real_sector_size > 4) {
          errx(1, "fsd file excessive sector size");
        }
        p_buf += 6;
        file_remaining -= 6;

        real_sector_size = (1 << (7 + real_sector_size));
        if (file_remaining < real_sector_size) {
          errx(1, "fsd file missing sector data");
        }
        track_data_bytes += real_sector_size;
        p_buf += real_sector_size;
        file_remaining -= real_sector_size;
      }
    }
    file_remaining = saved_file_remaining;
    p_buf = p_saved_buf;

    track_total_bytes = disc_fsd_calculate_track_total_bytes(fsd_sectors,
                                                             track_data_bytes,
                                                             gap1_ff_count,
                                                             gap2_ff_count,
                                                             gap3_ff_count);

    if (track_total_bytes > k_ibm_disc_bytes_per_track) {
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: excessive length track %d: %d (%d sectors %d data)",
                   i_track,
                   track_total_bytes,
                   fsd_sectors,
                   track_data_bytes);
      }

      /* This is ugly because the FSD format is ambiguous.
       * Where a sector's "real" size push us over the limit of 3125 bytes per
       * track, there are a couple of reasons this could be the case:
       * 1) The sectors are squished closer together than normal via small
       * inter-sector gaps.
       * 2) The "real" size isn't really the real size of data present, it's
       * just the next biggest size so that a small number of post-sector bytes
       * hidden in the inter-sector gap can be catered for.
       *
       * 2) is pretty common but if we follow it blindly we'll get incorrect
       * data for the case where 1) is going on.
       */

      /* First attempt -- see if it fits if we lower the gap sizes a lot. */
      /* NOTE: the 1770 is very sensitive to GAP2 size so we leave it alone.
       * In tests, 9 0xFF's is ok, 7 is not. 11 is the default.
       */
      gap1_ff_count = 3;
      gap3_ff_count = 3;
    }

    track_total_bytes = disc_fsd_calculate_track_total_bytes(fsd_sectors,
                                                             track_data_bytes,
                                                             gap1_ff_count,
                                                             gap2_ff_count,
                                                             gap3_ff_count);
    if (track_total_bytes > k_ibm_disc_bytes_per_track) {
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: small gaps, still excessive length: %d, truncating",
                   track_total_bytes);
      }
      do_data_truncate = 1;
      num_bytes_over = (track_total_bytes - k_ibm_disc_bytes_per_track);
    }

    /* Sync pattern at start of track, as the index pulse starts, aka GAP 1.
     * Note that GAP 5 (with index address mark) is typically not used in BBC
     * formatted discs.
     */
    disc_build_append_repeat(p_disc, 0xFF, gap1_ff_count);
    disc_build_append_repeat(p_disc, 0x00, 6);
    track_remaining -= (gap1_ff_count + 6);

    /* Pass 2: process data bytes. */
    for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
      uint8_t logical_track;
      uint8_t logical_head;
      uint8_t logical_sector;
      uint32_t logical_size;
      char sector_spec[12];
      uint32_t real_sector_size;
      uint8_t sector_error;

      if (file_remaining < 4) {
        errx(1, "fsd file missing sector header");
      }
      if (track_remaining < (7 + (gap2_ff_count + 6))) {
        errx(1, "fsd file track no space for sector header and gap");
      }
      /* Sector header, aka. ID. */
      disc_build_reset_crc(p_disc);
      disc_build_append_single_with_clocks(p_disc,
                                           k_ibm_disc_id_mark_data_pattern,
                                           k_ibm_disc_mark_clock_pattern);
      logical_track = p_buf[0];
      logical_head = p_buf[1];
      logical_sector = p_buf[2];
      logical_size = p_buf[3];
      (void) snprintf(sector_spec,
                      sizeof(sector_spec),
                      "%.2x/%.2x/%.2x/%.2x",
                      logical_track,
                      logical_head,
                      logical_sector,
                      logical_size);
      if ((logical_track != i_track) && log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: track mismatch physical %d: %s",
                   i_track,
                   sector_spec);
      }
      if (sector_seen[logical_sector] && log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: duplicate logical sector, track %d: %s",
                   i_track,
                   sector_spec);
      }
      sector_seen[logical_sector] = 1;

      disc_build_append_single(p_disc, logical_track);
      disc_build_append_single(p_disc, logical_head);
      disc_build_append_single(p_disc, logical_sector);
      disc_build_append_single(p_disc, logical_size);
      disc_build_append_crc(p_disc);

      /* Sync pattern between sector header and sector data, aka. GAP 2. */
      disc_build_append_repeat(p_disc, 0xFF, gap2_ff_count);
      disc_build_append_repeat(p_disc, 0x00, 6);
      p_buf += 4;
      file_remaining -= 4;
      track_remaining -= (7 + (gap2_ff_count + 6));

      if (do_read_data) {
        uint32_t data_write_size;

        int do_crc = 1;
        int do_crc_error = 0;
        int do_weak_bits = 0;
        uint8_t sector_mark = k_ibm_disc_data_mark_data_pattern;

        if (file_remaining < 2) {
          errx(1, "fsd file missing sector header second part");
        }

        real_sector_size = p_buf[0];
        if (real_sector_size > 4) {
          errx(1, "fsd file excessive sector size");
        }
        sector_error = p_buf[1];

        logical_size = (1 << (7 + logical_size));
        real_sector_size = (1 << (7 + real_sector_size));
        data_write_size = real_sector_size;
        //if (do_data_truncate) {
        //  data_write_size = logical_size;
        //}

        if (sector_error == 0x20) {
          /* Deleted data. */
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: deleted sector track %d: %s",
                       i_track,
                       sector_spec);
          }
          sector_mark = k_ibm_disc_deleted_data_mark_data_pattern;
        } else if (sector_error == 0x0E) {
          /* Sector has data CRC error. */
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: CRC error sector track %d: %s",
                       i_track,
                       sector_spec);
          }
          /* CRC error in the FSD format appears to also imply weak bits. See:
           * https://stardot.org.uk/forums/viewtopic.php?f=4&t=4353&start=30#p74208
           */
          do_crc_error = 1;
          /* Sector error $0E only applies weak bits if the real and declared
           * sector sizes match. Otherwise various Sherston Software titles
           * fail. This also matches the logic in:
           * https://github.com/stardot/beebem-windows/blob/fsd-disk-support/Src/disc8271.cpp
           */
          if (real_sector_size == logical_size) {
            do_weak_bits = 1;
          }
        } else if (sector_error == 0x2E) {
          /* $2E isn't documented and neither are $20 / $0E documented as bit
           * fields, but it shows up anyway in The Wizard's Return.
           */
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: deleted and CRC error sector track %d: %s",
                       i_track,
                       sector_spec);
          }
          sector_mark = k_ibm_disc_deleted_data_mark_data_pattern;
          do_crc_error = 1;
        } else if ((sector_error >= 0xE0) && (sector_error <= 0xE2)) {
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: multiple sector read sizes $%.2x track %d: %s",
                       sector_error,
                       i_track,
                       sector_spec);
          }
          do_crc_error = 1;
          if (do_data_truncate) {
            uint32_t new_bytes = 0;
            uint32_t orig_data_write_size = data_write_size;

            do_crc_error = 0;
            if (sector_error == 0xE0) {
              data_write_size = 128;
            } else if (sector_error == 0xE1) {
              data_write_size = 256;
            } else {
              data_write_size = 512;
            }
            if (data_write_size < orig_data_write_size) {
              new_bytes = (orig_data_write_size - data_write_size);
            }
            /* NOTE: this is a pretty random heuristic. We only add in sector
             * overread bytes if there's just one such sector. This keeps
             * things simple, but it might be necessary to share the
             * remaining track space across multiple overread sectors if we
             * ever hit that.
             */
            if ((num_multi_size_sectors == 1) &&
                (new_bytes > (num_bytes_over + 1))) {
              /* Don't emit a CRC at all -- it should be part of the overread
               * data.
               */
              do_crc = 0;
              data_write_size += (new_bytes - num_bytes_over - 1);
            }
          }
        } else if (sector_error != 0) {
          errx(1, "fsd file sector error %d unsupported", sector_error);
        }
        p_buf += 2;
        file_remaining -= 2;

        if (real_sector_size != logical_size) {
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: real size mismatch track %d size %d: %s",
                       i_track,
                       real_sector_size,
                       sector_spec);
          }
        }
        if (file_remaining < real_sector_size) {
          errx(1, "fsd file missing sector data");
        }
        if (track_remaining < (data_write_size + 3)) {
          errx(1, "fsd file track no space for sector data");
        }

        disc_build_reset_crc(p_disc);
        disc_build_append_single_with_clocks(p_disc,
                                             sector_mark,
                                             k_ibm_disc_mark_clock_pattern);
        if (!do_weak_bits) {
          disc_build_append_chunk(p_disc, p_buf, data_write_size);
        } else {
          /* This is icky: the titles that rely on weak bits (mostly,
           * hopefully exclusively? Sherston Software titles) rely on the weak
           * bits being a little later in the sector as the code at the start
           * of the sector is executed!!
           */
          disc_build_append_chunk(p_disc, p_buf, 24);
          /* Our 8271 driver interprets empty disc surface (no data bits, no
           * clock bits) as weak bits. As does my real drive + 8271 combo.
           */
          disc_build_append_repeat_with_clocks(p_disc, 0x00, 0x00, 8);
          disc_build_append_chunk(p_disc,
                                  (p_buf + 32),
                                  (data_write_size - 32));
        }
        if (do_crc) {
          if (do_crc_error) {
            disc_build_append_bad_crc(p_disc);
          } else {
            disc_build_append_crc(p_disc);
          }
        }

        p_buf += real_sector_size;
        file_remaining -= real_sector_size;
        track_remaining -= (data_write_size + 3);

        if ((fsd_sectors == 1) && (track_data_bytes == 256)) {
          if (log_protection) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "FSD: workaround: zero padding short track %d: %s",
                       i_track,
                       sector_spec);
          }
          /* This is essentially a workaround for buggy FSD files, such as:
           * 297 DISC DUPLICATOR 3.FSD
           * The copy protection relies on zeros being returned from a sector
           * overread of a single sectored short track, but the FSD file does
           * not guarantee this.
           * Also make sure to not accidentally create a valid CRC for a 512
           * byte read. This happens if the valid 256 byte sector CRC is
           * followed by all 0x00 and an 0x00 CRC.
           */
          disc_build_append_repeat(p_disc, 0x00, (256 - 2));
          disc_build_append_repeat(p_disc, 0xFF, 2);
          track_remaining -= 256;
        }
      }

      if (i_sector != (fsd_sectors - 1)) {
        /* Sync pattern between sectors, aka. GAP 3. */
        if (track_remaining < (gap3_ff_count + 6)) {
          errx(1, "fsd file track no space for inter sector gap");
        }
        disc_build_append_repeat(p_disc, 0xFF, gap3_ff_count);
        disc_build_append_repeat(p_disc, 0x00, 6);
        track_remaining -= (gap3_ff_count + 6);
      }
    } /* End of sectors loop. */

    /* Fill until end of track, aka. GAP 4. */
    assert(disc_get_head_position(p_disc) <= k_ibm_disc_bytes_per_track);
    disc_build_append_repeat(p_disc,
                             0xFF,
                             (k_ibm_disc_bytes_per_track -
                              disc_get_head_position(p_disc)));
  } /* End of track loop. */
}
