#include "disc_fsd.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  k_disc_fsd_max_sectors = 32,
};

struct disc_fsd_sector {
  char sector_spec[32];
  uint8_t sector_error;
  uint8_t logical_track;
  uint8_t head;
  uint8_t logical_sector;
  uint8_t logical_size;
  uint32_t logical_size_bytes;
  uint32_t actual_size_bytes;
  uint32_t truncated_size_bytes;
  uint32_t write_size_bytes;
  uint8_t* p_data;
  int is_deleted;
  int is_crc_error;
  int is_weak_bits;
  int is_crc_included;
  int is_format_bytes;
};

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

static void
disc_fsd_determine_sherston_weak_bits(struct disc_fsd_sector* p_sector) {
  /* Sector error $0E only applies weak bits if the real and declared
   * sector sizes match. Otherwise various Sherston Software titles
   * fail. This also matches the logic in:
   * https://github.com/stardot/beebem-windows/blob/fsd-disk-support/Src/disc8271.cpp
   */
  if (!p_sector->is_crc_error) {
    return;
  }

  /* Only sector error $0E needs to trigger. */
  if (p_sector->sector_error != 0x0E) {
    return;
  }

  /* Examples:
   * 1) 621 THREE LITTLE PIGS AT HOME - THE STORY.log
   * Track 39 / sector 9 / logical size 256. Physical size 128.
   *
   * 2) 186 THE TEDDY BEARS' PICNIC SIDE 1.FSD
   * Track 39 / sector 9 / logical size 256. Physical size 256.
   *
   * 3) 267 THE CIRCUS (80 TRACK).FSD
   * Track 0 / sector 2 / logical size 256. Physical size 256.
   */

  /* Sherston weak bits are track 39, sector 9 or track 0, sector 2. */
  if ((p_sector->logical_track == 39) && (p_sector->logical_sector == 9)) {
    /* Yes. */
  } else if ((p_sector->logical_track == 0) &&
             (p_sector->logical_sector == 2)) {
    /* Yes. */
  } else {
    return;
  }

  p_sector->is_weak_bits = 1;
}

static void
disc_fsd_parse_sectors(struct disc_fsd_sector* p_sectors,
                       uint32_t* p_track_data_bytes,
                       uint32_t* p_track_truncatable_bytes,
                       uint32_t* p_track_truncatable_sectors,
                       uint8_t** p_p_buf,
                       size_t* p_file_remaining,
                       uint32_t fsd_sectors,
                       uint32_t track,
                       int log_protection) {
  uint8_t sector_seen[256];
  uint32_t i_sector;

  int readable = 1;
  uint8_t* p_buf = *p_p_buf;
  size_t file_remaining = *p_file_remaining;

  (void) memset(sector_seen, '\0', sizeof(sector_seen));
  (void) memset(p_sectors,
                '\0',
                (sizeof(struct disc_fsd_sector) * k_disc_fsd_max_sectors));

  *p_track_data_bytes = 0;
  *p_track_truncatable_bytes = 0;
  *p_track_truncatable_sectors = 0;

  if (fsd_sectors > k_disc_fsd_max_sectors) {
    util_bail("fsd file excessive sectors");
  }

  if ((fsd_sectors != 10) && log_protection) {
    log_do_log(k_log_disc,
               k_log_info,
               "FSD: non-standard sector count track %d count %d",
               track,
               fsd_sectors);
  }

  if (file_remaining == 0) {
    util_bail("fsd file missing readable flag");
  }

  if (*p_buf == 0) {
    /* "unreadable" track. */
    if (log_protection) {
      log_do_log(k_log_disc, k_log_info, "FSD: unreadable track %d", track);
    }
    readable = 0;
  } else if (*p_buf != 0xFF) {
    util_bail("fsd file unknown readable byte value");
  }
  p_buf++;
  file_remaining--;

  for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
    uint8_t logical_sector;
    uint8_t logical_size;
    uint32_t actual_size_bytes;
    uint32_t logical_size_bytes;
    uint8_t sector_error;

    struct disc_fsd_sector* p_sector = &p_sectors[i_sector];

    if (file_remaining < 4) {
      util_bail("fsd file missing sector header");
    }

    p_sector->logical_track = p_buf[0];
    p_sector->head = p_buf[1];
    logical_sector = p_buf[2];
    p_sector->logical_sector = logical_sector;
    logical_size = p_buf[3];
    p_sector->logical_size = logical_size;
    p_buf += 4;
    file_remaining -= 4;

    (void) snprintf(p_sector->sector_spec,
                    sizeof(p_sector->sector_spec),
                    "$%.2x/$%.2x/$%.2x/$%.2x",
                    p_sector->logical_track,
                    p_sector->head,
                    logical_sector,
                    p_sector->logical_size);
    if ((p_sector->logical_track != track) && log_protection) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "FSD: track mismatch physical %d: %s",
                 track,
                 p_sector->sector_spec);
    }
    if (sector_seen[logical_sector] && log_protection) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "FSD: duplicate logical sector, track %d: %s",
                 track,
                 p_sector->sector_spec);
    }
    sector_seen[logical_sector] = 1;

    if (!readable) {
      /* Invent some "unreadable" data. I looked at Exile's "unreadable"
       * track and the bytes are just format bytes, i.e. 0xE5.
       */
      uint32_t num_format_bytes = 128;
      if (fsd_sectors <= 10) {
        num_format_bytes = 256;
      }

      p_sector->is_format_bytes = 1;
      p_sector->write_size_bytes = num_format_bytes;

      continue;
    }

    if (file_remaining < 2) {
      util_bail("fsd file missing sector header");
    }

    actual_size_bytes = p_buf[0];
    sector_error = p_buf[1];
    p_sector->sector_error = sector_error;
    p_buf += 2;
    file_remaining -= 2;

    if (actual_size_bytes > 4) {
      util_bail("fsd file excessive sector size");
    }
    actual_size_bytes = (1 << (7 + actual_size_bytes));
    p_sector->actual_size_bytes = actual_size_bytes;
    p_sector->truncated_size_bytes = actual_size_bytes;
    p_sector->write_size_bytes = actual_size_bytes;
    if (file_remaining < actual_size_bytes) {
      util_bail("fsd file missing sector data");
    }
    p_sector->p_data = p_buf;
    p_buf += actual_size_bytes;
    file_remaining -= actual_size_bytes;

    *p_track_data_bytes += actual_size_bytes;
    logical_size_bytes = (1 << (7 + logical_size));

    if ((actual_size_bytes != logical_size_bytes) && log_protection) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "FSD: real size mismatch track %d size %d: %s",
                 track,
                 actual_size_bytes,
                 p_sector->sector_spec);
    }

    if (logical_size_bytes < actual_size_bytes) {
      p_sector->truncated_size_bytes = logical_size_bytes;
    }

    if (sector_error == 0x20) {
      /* Deleted data. */
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: deleted sector track %d: %s",
                   track,
                   p_sector->sector_spec);
      }
      p_sector->is_deleted = 1;
    } else if (sector_error == 0x0E) {
      /* Sector has data CRC error. */
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: CRC error sector track %d: %s",
                   track,
                   p_sector->sector_spec);
      }
      /* CRC error in the FSD format appears to also imply weak bits. See:
       * https://stardot.org.uk/forums/viewtopic.php?f=4&t=4353&start=30#p74208
       */
      p_sector->is_crc_error = 1;
    } else if (sector_error == 0x2E) {
      /* $2E isn't documented and neither are $20 / $0E documented as bit
       * fields, but it shows up anyway in The Wizard's Return.
       */
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: deleted and CRC error sector track %d: %s",
                   track,
                   p_sector->sector_spec);
      }
      p_sector->is_crc_error = 1;
      p_sector->is_deleted = 1;
    } else if ((sector_error >= 0xE0) && (sector_error <= 0xE2)) {
      if (log_protection) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "FSD: multiple sector read sizes $%.2x track %d: %s",
                   sector_error,
                   track,
                   p_sector->sector_spec);
      }
      p_sector->is_crc_error = 1;
      if (sector_error == 0xE0) {
        p_sector->truncated_size_bytes = 128;
      } else if (sector_error == 0xE1) {
        if (p_sector->actual_size_bytes < 256) {
          util_bail("bad size for sector error $E1");
        }
        p_sector->truncated_size_bytes = 256;
      } else {
        if (p_sector->actual_size_bytes < 512) {
          util_bail("bad size for sector error $E2");
        }
        p_sector->truncated_size_bytes = 512;
      }
    } else if (sector_error != 0) {
      util_bail("fsd file sector error %d unsupported", sector_error);
    }

    /* For now, try and determine whether it's a Sherston Software weak bits
     * sector from just the header details.
     */
    disc_fsd_determine_sherston_weak_bits(p_sector);

    if (p_sector->truncated_size_bytes != p_sector->actual_size_bytes) {
      (*p_track_truncatable_sectors)++;
      *p_track_truncatable_bytes += (p_sector->actual_size_bytes -
                                     p_sector->truncated_size_bytes);
    }
  }

  *p_p_buf = p_buf;
  *p_file_remaining = file_remaining;
}

static void
disc_fsd_perform_track_adjustments(struct disc_fsd_sector* p_sectors,
                                   uint32_t* p_gap1_ff_count,
                                   uint32_t* p_gap3_ff_count,
                                   uint32_t gap2_ff_count,
                                   uint32_t fsd_sectors,
                                   uint32_t track_data_bytes,
                                   uint32_t track_truncatable_bytes,
                                   uint32_t track_truncatable_sectors,
                                   uint32_t track,
                                   int log_protection) {
  uint32_t track_total_bytes;
  uint32_t num_bytes_over;
  uint32_t overread_bytes_per_sector;
  uint32_t i_sector;

  track_total_bytes = disc_fsd_calculate_track_total_bytes(fsd_sectors,
                                                           track_data_bytes,
                                                           *p_gap1_ff_count,
                                                           gap2_ff_count,
                                                           *p_gap3_ff_count);

  if (track_total_bytes <= k_ibm_disc_bytes_per_track) {
    return;
  }

  if (log_protection) {
    log_do_log(k_log_disc,
               k_log_info,
               "FSD: excessive length track %d: %d (%d sectors %d data)",
               track,
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
  /* NOTE: the 1770 and 8271 controllers are very sensitive to GAP2 size so we
   * leave it alone. (In tests, 9 0xFF's is ok, 7 is not. 11 is the default.)
   * These gaps here are sufficient to enable certain tricky Tynesoft titles to
   * work (Winter Olympiad, Boulderdash, etc.), which have very small inter-
   * sector gaps. On the Winter Olympiad disc, the packed tracks have total
   * gap sizes (FFs + 00s) as small as 4, but more typically 7 (1x FFs, 6x 00s).
   * The total gap sizes here will be 9, which is still small enough to work.
   */
  *p_gap1_ff_count = 3;
  *p_gap3_ff_count = 3;

  track_total_bytes = disc_fsd_calculate_track_total_bytes(fsd_sectors,
                                                           track_data_bytes,
                                                           *p_gap1_ff_count,
                                                           gap2_ff_count,
                                                           *p_gap3_ff_count);
  if (track_total_bytes <= k_ibm_disc_bytes_per_track) {
    return;
  }

  if (log_protection) {
    log_do_log(k_log_disc,
               k_log_info,
               "FSD: small gaps, still excessive length: %d, truncating",
               track_total_bytes);
  }

  num_bytes_over = (track_total_bytes - k_ibm_disc_bytes_per_track);
  if (num_bytes_over >= track_truncatable_bytes) {
    util_bail("fsd sectors really cannot fit");
  }

  track_total_bytes -= track_truncatable_bytes;
  overread_bytes_per_sector = (k_ibm_disc_bytes_per_track -
                               track_total_bytes -
                               1);
  overread_bytes_per_sector /= track_truncatable_sectors;

  for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
    struct disc_fsd_sector* p_sector = &p_sectors[i_sector];

    if (p_sector->write_size_bytes == p_sector->truncated_size_bytes) {
      continue;
    }

    /* After truncating all the truncatable sectors, there is likely space left
     * in the track to include sector overread bytes. Spread these available
     * overread bytes across all sectors that were truncated.
     * When we add overread bytes, the CRC is part of the overread bytes, and
     * no longer something we manually calculate and append.
     */
    p_sector->write_size_bytes = p_sector->truncated_size_bytes;
    p_sector->write_size_bytes += overread_bytes_per_sector;
    p_sector->is_crc_included = 1;
  }
}

void
disc_fsd_load(struct disc_struct* p_disc,
              int has_file_name,
              int log_protection) {
  /* The most authoritative "documentation" for the FSD format appears to be:
   * https://stardot.org.uk/forums/viewtopic.php?f=4&t=4353&start=60#p195518
   */
  static const size_t k_max_fsd_size = (1024 * 1024);
  uint8_t* p_file_buf;
  size_t len;
  size_t file_remaining;
  uint8_t* p_buf;
  uint32_t fsd_tracks;
  uint32_t i_track;
  uint8_t title_char;

  struct util_file* p_file = disc_get_file(p_disc);
  assert(p_file != NULL);

  p_file_buf = util_malloc(k_max_fsd_size);

  len = util_file_read(p_file, p_file_buf, k_max_fsd_size);

  if (len == k_max_fsd_size) {
    util_bail("fsd file too large");
  }

  p_buf = p_file_buf;
  file_remaining = len;
  if (file_remaining < 8) {
    util_bail("fsd file no header");
  }
  if (memcmp(p_buf, "FSD", 3) != 0) {
    util_bail("fsd file incorrect header");
  }
  p_buf += 8;
  file_remaining -= 8;
  if (has_file_name) {
    do {
      if (file_remaining == 0) {
        util_bail("fsd file missing title");
      }
      title_char = *p_buf;
      p_buf++;
      file_remaining--;
    } while (title_char != 0);
  }

  if (file_remaining == 0) {
    util_bail("fsd file missing tracks");
  }
  /* This appears to actually be "max zero-indexed track ID" so we add 1. */
  fsd_tracks = *p_buf;
  fsd_tracks++;
  p_buf++;
  file_remaining--;
  if (fsd_tracks > k_ibm_disc_tracks_per_disc) {
    util_bail("fsd file too many tracks: %d", fsd_tracks);
  }

  for (i_track = 0; i_track < fsd_tracks; ++i_track) {
    struct disc_fsd_sector sectors[k_disc_fsd_max_sectors];
    uint32_t fsd_sectors;
    uint32_t i_sector;

    uint32_t track_remaining = k_ibm_disc_bytes_per_track;
    uint32_t track_data_bytes = 0;
    uint32_t track_truncatable_bytes = 0;
    uint32_t track_truncatable_sectors = 0;
    /* Acorn format command standards for 256 byte sectors. The 8271 datasheet
     * generally agrees but does suggest 21 for GAP3.
     */
    uint32_t gap1_ff_count = 16;
    uint32_t gap2_ff_count = 11;
    uint32_t gap3_ff_count = 16;

    /* Some files end prematurely but cleanly on a track boundary. These files
     * seem to work ok if the remainder of tracks are left unformatted.
     * Examples:
     * 255 Felix meets the Evil Weevils.FSD
     * 518 THE WORST WITCH.log
     */
    if (file_remaining == 0) {
      break;
    }

    if (file_remaining < 2) {
      util_bail("fsd file missing track header");
    }
    if (p_buf[0] != i_track) {
      util_bail("fsd file unmatched track id");
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
      /* NOTE: "unformatted" track could mean a few different possibilities.
       * What it definitely means is that there are no detectable sector ID
       * markers. But it doesn't say why.
       * For example, my original Elite and Castle Quest discs have a genuinely
       * unformatted track, i.e. no flux transitions. On the other hand, my
       * original Labyrinth disc has flux transitions (of varinging width)!
       * Let's go with a genuinely unformatted track.
       */
      disc_build_append_repeat_with_clocks(p_disc,
                                           0,
                                           0,
                                           k_ibm_disc_bytes_per_track);
      continue;
    }

    (void) memset(sectors, '\0', sizeof(sectors));
    disc_fsd_parse_sectors(sectors,
                           &track_data_bytes,
                           &track_truncatable_bytes,
                           &track_truncatable_sectors,
                           &p_buf,
                           &file_remaining,
                           fsd_sectors,
                           i_track,
                           log_protection);

    if (fsd_sectors > 10) {
      /* Standard for 128 byte sectors. If we didn't lower the value here, the
       * track wouldn't fit.
       */
      gap3_ff_count = 11;
    }

    disc_fsd_perform_track_adjustments(sectors,
                                       &gap1_ff_count,
                                       &gap3_ff_count,
                                       gap2_ff_count,
                                       fsd_sectors,
                                       track_data_bytes,
                                       track_truncatable_bytes,
                                       track_truncatable_sectors,
                                       i_track,
                                       log_protection);

    /* Sync pattern at start of track, as the index pulse starts, aka GAP 1.
     * Note that GAP 5 (with index address mark) is typically not used in BBC
     * formatted discs.
     */
    disc_build_append_repeat(p_disc, 0xFF, gap1_ff_count);
    disc_build_append_repeat(p_disc, 0x00, 6);
    track_remaining -= (gap1_ff_count + 6);

    for (i_sector = 0; i_sector < fsd_sectors; ++i_sector) {
      struct disc_fsd_sector* p_sector = &sectors[i_sector];
      uint32_t write_size_bytes = p_sector->write_size_bytes;
      uint8_t* p_data = p_sector->p_data;
      uint8_t sector_mark = k_ibm_disc_data_mark_data_pattern;

      if (track_remaining < (7 + (gap2_ff_count + 6))) {
        util_bail("fsd file track no space for sector header and gap");
      }
      /* Sector header, aka. ID. */
      disc_build_reset_crc(p_disc);
      disc_build_append_single_with_clocks(p_disc,
                                           k_ibm_disc_id_mark_data_pattern,
                                           k_ibm_disc_mark_clock_pattern);
      disc_build_append_single(p_disc, p_sector->logical_track);
      disc_build_append_single(p_disc, p_sector->head);
      disc_build_append_single(p_disc, p_sector->logical_sector);
      disc_build_append_single(p_disc, p_sector->logical_size);
      disc_build_append_crc(p_disc);

      /* Sync pattern between sector header and sector data, aka. GAP 2. */
      disc_build_append_repeat(p_disc, 0xFF, gap2_ff_count);
      disc_build_append_repeat(p_disc, 0x00, 6);
      track_remaining -= (7 + (gap2_ff_count + 6));

      if (p_sector->is_deleted) {
        sector_mark = k_ibm_disc_deleted_data_mark_data_pattern;
      }

      if (track_remaining < (write_size_bytes + 3)) {
        util_bail("fsd file track no space for sector data");
      }

      disc_build_reset_crc(p_disc);
      disc_build_append_single_with_clocks(p_disc,
                                           sector_mark,
                                           k_ibm_disc_mark_clock_pattern);
      if (p_sector->is_format_bytes) {
        disc_build_append_repeat(p_disc, 0xE5, write_size_bytes);
      } else if (!p_sector->is_weak_bits) {
        disc_build_append_chunk(p_disc, p_data, write_size_bytes);
      } else {
        /* This is icky: the titles that rely on weak bits (mostly,
         * hopefully exclusively? Sherston Software titles) rely on the weak
         * bits being a little later in the sector as the code at the start
         * of the sector is executed!!
         */
        disc_build_append_chunk(p_disc, p_data, 24);
        /* Our 8271 driver interprets empty disc surface (no data bits, no
         * clock bits) as weak bits. As does my real drive + 8271 combo.
         */
        disc_build_append_repeat_with_clocks(p_disc, 0x00, 0x00, 8);
        disc_build_append_chunk(p_disc,
                                (p_data + 32),
                                (write_size_bytes - 32));
      }
      if (!p_sector->is_crc_included) {
        if (p_sector->is_crc_error) {
          disc_build_append_bad_crc(p_disc);
        } else {
          disc_build_append_crc(p_disc);
        }
      }

      track_remaining -= (write_size_bytes + 3);

      if ((fsd_sectors == 1) && (track_data_bytes == 256)) {
        if (log_protection) {
          log_do_log(k_log_disc,
                     k_log_info,
                     "FSD: workaround: zero padding short track %d: %s",
                     i_track,
                     p_sector->sector_spec);
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

      if (i_sector != (fsd_sectors - 1)) {
        /* Sync pattern between sectors, aka. GAP 3. */
        if (track_remaining < (gap3_ff_count + 6)) {
          util_bail("fsd file track no space for inter sector gap");
        }
        disc_build_append_repeat(p_disc, 0xFF, gap3_ff_count);
        disc_build_append_repeat(p_disc, 0x00, 6);
        track_remaining -= (gap3_ff_count + 6);
      }
    } /* End of sectors loop. */

    /* Fill until end of track, aka. GAP 4. */
    disc_build_fill(p_disc, 0xFF);
  } /* End of track loop. */

  util_free(p_file_buf);
}
