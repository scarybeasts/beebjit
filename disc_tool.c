#include "disc_tool.h"

#include "ibm_disc_format.h"
#include "disc.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
  k_max_sectors = 32,
};

struct disc_tool_struct {
  struct disc_struct* p_disc;
  int is_side_upper;
  uint32_t track;
  uint32_t track_length;
  uint32_t pos;
  struct disc_tool_sector sectors[k_max_sectors];
  uint32_t num_sectors;
};

struct disc_tool_struct*
disc_tool_create() {
  struct disc_tool_struct* p_tool =
      util_mallocz(sizeof(struct disc_tool_struct));

  return p_tool;
}

void
disc_tool_destroy(struct disc_tool_struct* p_tool) {
  util_free(p_tool);
}

uint32_t
disc_tool_get_track(struct disc_tool_struct* p_tool) {
  return p_tool->track;
}

uint32_t
disc_tool_get_byte_pos(struct disc_tool_struct* p_tool) {
  return (p_tool->pos / 32);
}

void
disc_tool_set_disc(struct disc_tool_struct* p_tool,
                   struct disc_struct* p_disc) {
  p_tool->p_disc = p_disc;
  disc_tool_set_track(p_tool, p_tool->track);
}

void
disc_tool_set_is_side_upper(struct disc_tool_struct* p_tool,
                            int is_side_upper) {
  p_tool->is_side_upper = is_side_upper;
  disc_tool_set_track(p_tool, p_tool->track);
}

void
disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track) {
  p_tool->track = track;
  p_tool->num_sectors = 0;
  p_tool->track_length = 0;
  if (p_tool->p_disc == NULL) {
    return;
  }

  p_tool->track_length = disc_get_track_length(p_tool->p_disc,
                                               p_tool->is_side_upper,
                                               p_tool->track);
}

void
disc_tool_set_byte_pos(struct disc_tool_struct* p_tool, uint32_t pos) {
  if (pos >= p_tool->track_length) {
    pos = 0;
  }
  p_tool->pos = (pos * 32);
}

static uint32_t*
disc_tool_get_pulses(struct disc_tool_struct* p_tool) {
  struct disc_struct* p_disc = p_tool->p_disc;
  int is_side_upper = p_tool->is_side_upper;
  uint32_t track = p_tool->track;

  if (p_disc == NULL) {
    return NULL;
  }

  if (track >= k_ibm_disc_tracks_per_disc) {
    return NULL;
  }

  return disc_get_raw_pulses_buffer(p_disc, is_side_upper, track);
}

static uint32_t
disc_tool_read_pulses(struct disc_tool_struct* p_tool) {
  uint32_t pulses;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  uint32_t pos = p_tool->pos;
  uint32_t pulses_pos = (pos / 32);
  uint32_t bit_pos = (pos % 32);
  uint32_t source_pulses;

  if (p_pulses == NULL) {
    return 0;
  }

  source_pulses = p_pulses[pulses_pos];
  pulses = (source_pulses << bit_pos);
  if (pulses_pos == p_tool->track_length) {
    pulses_pos = 0;
    p_tool->pos = bit_pos;
  } else {
    pulses_pos++;
    p_tool->pos += 32;
  }
  if (bit_pos > 0) {
    source_pulses = p_pulses[pulses_pos];
    pulses |= (source_pulses >> (32 - bit_pos));
  }

  return pulses;
}

void
disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                       uint8_t* p_clocks,
                       uint8_t* p_data,
                       int* p_is_iffy_pulse,
                       uint32_t len) {
  uint32_t i;
  int is_iffy_pulse = 0;

  for (i = 0; i < len; ++i) {
    uint8_t clocks;
    uint8_t data;
    int is_this_iffy_pulse;
    uint32_t pulses = disc_tool_read_pulses(p_tool);
    ibm_disc_format_2us_pulses_to_fm(&clocks,
                                     &data,
                                     &is_this_iffy_pulse,
                                     pulses);
    if (p_clocks != NULL) {
      p_clocks[i] = clocks;
    }
    if (p_data != NULL) {
      p_data[i] = data;
    }
    is_iffy_pulse |= is_this_iffy_pulse;
  }
  *p_is_iffy_pulse = is_iffy_pulse;
}

void
disc_tool_read_mfm_data(struct disc_tool_struct* p_tool,
                       uint8_t* p_data,
                       uint32_t len) {
  uint32_t i;
  uint32_t pulses = 0;

  for (i = 0; i < len; ++i) {
    uint8_t data;
    if ((i & 1) == 0) {
      pulses = disc_tool_read_pulses(p_tool);
    } else {
      pulses <<= 16;
    }
    data = ibm_disc_format_2us_pulses_to_mfm(pulses >> 16);
    p_data[i] = data;
  }
}

static void
disc_tool_commit_write(struct disc_tool_struct* p_tool) {
  struct disc_struct* p_disc = p_tool->p_disc;
  if (p_disc == NULL) {
    return;
  }

  disc_dirty_and_flush(p_disc, p_tool->is_side_upper, p_tool->track);
}

void
disc_tool_write_fm_data(struct disc_tool_struct* p_tool,
                        uint8_t* p_clocks,
                        uint8_t* p_data,
                        uint32_t len) {
  uint32_t i;
  uint32_t pos = p_tool->pos;
  uint32_t pulses_pos = (pos / 32);
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  uint32_t track_length = p_tool->track_length;

  assert((pos % 32) == 0);

  if (p_pulses == NULL) {
    return;
  }

  for (i = 0; i < len; ++i) {
    uint32_t pulses;
    uint8_t clocks = 0xFF;
    if (p_clocks != NULL) {
      clocks = p_clocks[i];
    }
    pulses = ibm_disc_format_fm_to_2us_pulses(clocks, p_data[i]);
    p_pulses[pulses_pos] = pulses;
    pulses_pos++;
    if (pulses_pos == track_length) {
      pulses_pos = 0;
      pos = 0;
    } else {
      pos += 32;
    }
  }

  p_tool->pos = pos;

  disc_tool_commit_write(p_tool);
}

void
disc_tool_fill_fm_data(struct disc_tool_struct* p_tool, uint8_t data) {
  uint32_t i;
  uint32_t pulses;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    return;
  }

  pulses = ibm_disc_format_fm_to_2us_pulses(0xFF, data);

  for (i = 0; i < k_ibm_disc_bytes_per_track; ++i) {
    p_pulses[i] = pulses;
  }

  p_tool->pos = 0;

  disc_tool_commit_write(p_tool);
}

static uint16_t
disc_tool_crc_add_run(uint16_t crc, uint8_t* p_data, uint32_t length) {
  uint32_t i;
  for (i = 0; i < length; ++i) {
    crc = ibm_disc_format_crc_add_byte(crc, p_data[i]);
  }
  return crc;
}

void
disc_tool_find_sectors(struct disc_tool_struct* p_tool) {
  uint32_t i_pulses;
  uint32_t i_sectors;
  uint32_t bit_length;
  uint32_t shift_register;
  uint32_t num_shifts;
  int do_mfm_marker_byte;
  uint32_t pulses = 0;
  int is_mfm = 0;
  uint32_t num_sectors = 0;
  uint64_t mark_detector = 0;
  uint64_t mark_detector_prev = 0;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  struct disc_struct* p_disc = p_tool->p_disc;
  struct disc_tool_sector* p_sector = NULL;
  uint32_t track_length = p_tool->track_length;

  p_tool->num_sectors = 0;

  if (p_disc == NULL) {
    return;
  }

  /* Pass 1: walk the track and find header and data markers. */
  bit_length = (track_length * 32);
  shift_register = 0;
  num_shifts = 0;
  do_mfm_marker_byte = 0;
  for (i_pulses = 0; i_pulses < bit_length; ++i_pulses) {
    uint8_t clocks;
    uint8_t data;

    if ((i_pulses & 31) == 0) {
      pulses = p_pulses[i_pulses / 32];
    }
    mark_detector_prev <<= 1;
    mark_detector_prev |= (mark_detector >> 63);
    mark_detector <<= 1;
    shift_register <<= 1;
    num_shifts++;
    if (pulses & 0x80000000) {
      mark_detector |= 1;
      shift_register |= 1;
    }
    pulses <<= 1;

    if ((mark_detector & 0xFFFFFFFF00000000) == 0x8888888800000000) {
      uint32_t num_0s;
      uint64_t mark_detector_prev_copy;
      int is_iffy_pulse;
      /* Check byte for FM marker. */
      ibm_disc_format_2us_pulses_to_fm(&clocks,
                                       &data,
                                       &is_iffy_pulse,
                                       mark_detector);
      if (is_iffy_pulse || (clocks != k_ibm_disc_mark_clock_pattern)) {
        continue;
      }
      is_mfm = 0;
      do_mfm_marker_byte = 0;
      num_0s = 8;
      mark_detector_prev_copy = mark_detector_prev;
      while ((mark_detector_prev_copy & 0xF) == 0x8) {
        num_0s++;
        mark_detector_prev_copy >>= 4;
      }
      if (num_0s <= 16) {
        log_do_log(k_log_disc,
                   k_log_unusual,
                   "short zeros sync side %d track %d: %d",
                   p_tool->is_side_upper,
                   p_tool->track,
                   num_0s);
      }
    } else if (mark_detector == 0xAAAA448944894489) {
      /* Next byte is MFM marker. */
      is_mfm = 1;
      do_mfm_marker_byte = 1;
      shift_register = 0;
      num_shifts = 0;
      continue;
    } else if (do_mfm_marker_byte && (num_shifts == 16)) {
      data = ibm_disc_format_2us_pulses_to_mfm(shift_register);
      do_mfm_marker_byte = 0;
    } else {
      continue;
    }

    if (data == k_ibm_disc_id_mark_data_pattern) {
      if (num_sectors == k_max_sectors) {
        util_bail("too many sector headers");
      }
      p_sector = &p_tool->sectors[num_sectors];
      (void) memset(p_sector, '\0', sizeof(struct disc_tool_sector));
      num_sectors++;
      p_sector->is_mfm = is_mfm;
      p_sector->bit_pos_header = (i_pulses + 1);
      shift_register = 0;
      num_shifts = 0;
    } else if ((data == k_ibm_disc_data_mark_data_pattern) ||
               (data == k_ibm_disc_deleted_data_mark_data_pattern)) {
      if ((p_sector == NULL) || (p_sector->bit_pos_data != 0)) {
        log_do_log(k_log_disc,
                   k_log_unusual,
                   "sector data without header side %d track %d",
                   p_tool->is_side_upper,
                   p_tool->track);
      } else {
        assert(p_sector->bit_pos_header != 0);
        p_sector->bit_pos_data = (i_pulses + 1);
        if (data == k_ibm_disc_deleted_data_mark_data_pattern) {
          p_sector->is_deleted = 1;
        }
        shift_register = 0;
        num_shifts = 0;
      }
    } else {
      log_do_log(k_log_disc,
                 k_log_unusual,
                 "encountered marker byte %.2X side %d track %d",
                 data,
                 p_tool->is_side_upper,
                 p_tool->track);
    }
  }

  /* Pass 2: walk the list of header markers, work out physical sector sizes,
   * and check CRCs.
   */
  for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
    uint8_t data;
    uint16_t crc;
    uint16_t disc_crc;
    struct disc_tool_sector* p_sector = &p_tool->sectors[i_sectors];
    uint8_t sector_data[k_disc_tool_max_sector_length + 2];
    uint32_t sector_start_byte;
    uint32_t sector_end_byte;
    uint32_t sector_size;
    uint32_t pulses_per_byte;

    assert(p_sector->bit_pos_header != 0);

    p_tool->pos = p_sector->bit_pos_header;
    if (p_sector->is_mfm) {
      pulses_per_byte = 16;
      disc_tool_read_mfm_data(p_tool, &p_sector->header_bytes[0], 6);
    } else {
      int is_iffy_pulse;
      pulses_per_byte = 32;
      disc_tool_read_fm_data(p_tool,
                             NULL,
                             &p_sector->header_bytes[0],
                             &is_iffy_pulse,
                             6);
      if (is_iffy_pulse) {
        log_do_log(k_log_disc,
                   k_log_unusual,
                   "iffy pulse in sector header side %d track %d",
                   p_tool->is_side_upper,
                   p_tool->track);
      }
    }
    crc = ibm_disc_format_crc_init(0);
    if (p_sector->is_mfm) {
      crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
      crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
      crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
    }
    crc = ibm_disc_format_crc_add_byte(crc, k_ibm_disc_id_mark_data_pattern);
    crc = disc_tool_crc_add_run(crc, &p_sector->header_bytes[0], 4);
    disc_crc = (p_sector->header_bytes[4] << 8);
    disc_crc |= p_sector->header_bytes[5];
    if (crc != disc_crc) {
      p_sector->has_header_crc_error = 1;
    }

    if (p_sector->bit_pos_data == 0) {
      log_do_log(k_log_disc,
                 k_log_unusual,
                 "sector header without data side %d track %d",
                 p_tool->is_side_upper,
                 p_tool->track);
      continue;
    }

    if (p_sector->is_deleted) {
      data = k_ibm_disc_deleted_data_mark_data_pattern;
    } else {
      data = k_ibm_disc_data_mark_data_pattern;
    }

    sector_start_byte = (p_sector->bit_pos_data / pulses_per_byte);
    sector_end_byte = track_length;
    if (i_sectors != (num_sectors - 1)) {
      sector_end_byte = (p_tool->sectors[i_sectors + 1].bit_pos_header /
                         pulses_per_byte);
    }
    sector_size = (sector_end_byte - sector_start_byte);
    /* Account for CRC and sync bytes. */
    sector_size -= 5;
    if (sector_size < 256) {
      sector_size = 128;
    } else if (sector_size < 512) {
      sector_size = 256;
    } else if (sector_size < 1024) {
      sector_size = 512;
    } else if (sector_size < 2048) {
      sector_size = 1024;
    } else {
      sector_size = k_disc_tool_max_sector_length;
    }

    p_sector->has_data_crc_error = 1;
    do {
      p_sector->byte_length = sector_size;

      p_tool->pos = p_sector->bit_pos_data;
      crc = ibm_disc_format_crc_init(0);
      if (p_sector->is_mfm) {
        crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
        crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
        crc = ibm_disc_format_crc_add_byte(crc, 0xA1);
      }
      crc = ibm_disc_format_crc_add_byte(crc, data);
      if (p_sector->is_mfm) {
        disc_tool_read_mfm_data(p_tool, &sector_data[0], (sector_size + 2));
      } else {
        int is_iffy_pulse;
        disc_tool_read_fm_data(p_tool,
                               NULL,
                               &sector_data[0],
                               &is_iffy_pulse,
                               (sector_size + 2));
        if (is_iffy_pulse) {
          log_do_log(k_log_disc,
                     k_log_unusual,
                     "iffy pulse in sector data side %d track %d",
                     p_tool->is_side_upper,
                     p_tool->track);
        }
      }
      crc = disc_tool_crc_add_run(crc, &sector_data[0], sector_size);
      disc_crc = (sector_data[sector_size] << 8);
      disc_crc |= sector_data[sector_size + 1];
      if (crc == disc_crc) {
        p_sector->has_data_crc_error = 0;
        break;
      }
      sector_size /= 2;
    } while (sector_size >= 128);
  }

  p_tool->num_sectors = num_sectors;
}

struct disc_tool_sector*
disc_tool_get_sectors(struct disc_tool_struct* p_tool,
                      uint32_t* p_num_sectors) {
  *p_num_sectors = p_tool->num_sectors;
  return &p_tool->sectors[0];
}

void
disc_tool_read_sector(struct disc_tool_struct* p_tool,
                      uint32_t* p_byte_length,
                      uint8_t* p_data,
                      uint32_t sector,
                      int do_include_marker) {
  struct disc_tool_sector* p_sector;
  uint32_t byte_length;

  if (p_byte_length != NULL) {
    *p_byte_length = 0;
  }

  if (sector >= p_tool->num_sectors) {
    return;
  }

  p_sector = &p_tool->sectors[sector];
  byte_length = p_sector->byte_length;
  if (byte_length == 0) {
    return;
  }
  p_tool->pos = p_sector->bit_pos_data;
  if (do_include_marker) {
    byte_length++;
    assert(p_tool->pos >= 32);
    if (p_sector->is_mfm) {
       p_tool->pos -= 16;
    } else {
       p_tool->pos -= 32;
    }
  }
  if (p_sector->is_mfm) {
    disc_tool_read_mfm_data(p_tool, p_data, byte_length);
  } else {
    int is_iffy_pulse;
    disc_tool_read_fm_data(p_tool, NULL, p_data, &is_iffy_pulse, byte_length);
  }
  if (p_byte_length != NULL) {
    *p_byte_length = byte_length;
  }
}

void
disc_tool_log_summary(struct disc_struct* p_disc,
                      int log_crc_errors,
                      int log_protection,
                      int log_fingerprint,
                      int log_fingerprint_tracks,
                      int do_dump_sector_data) {
  uint32_t i_sides;
  struct disc_tool_struct* p_tool = disc_tool_create();
  uint32_t max_track = disc_get_num_tracks_used(p_disc);
  int is_fingerprinting = (log_fingerprint || log_fingerprint_tracks);
  struct util_file* p_raw_dump_file = NULL;
  uint32_t num_sides = 1;
  int is_80t = 0;

  if (do_dump_sector_data) {
    char new_file_name[4096];
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.dump",
                    disc_get_file_name(p_disc));
    log_do_log(k_log_disc, k_log_info, "dumping sectors to: %s", new_file_name);
    p_raw_dump_file = util_file_open(new_file_name, 1, 1);
  }

  if (disc_is_double_sided(p_disc)) {
    num_sides = 2;
  }
  if (max_track < 41) {
    max_track = 41;
  } else if (max_track > 42) {
    is_80t = 1;
    if (max_track < 81) {
      max_track = 81;
    }
  }

  disc_tool_set_disc(p_tool, p_disc);
  for (i_sides = 0; i_sides < num_sides; ++i_sides) {
    uint32_t i_tracks;
    uint32_t disc_crc = util_crc32_init();
    uint32_t disc_crc_even = util_crc32_init();

    disc_tool_set_is_side_upper(p_tool, (i_sides == 1));

    for (i_tracks = 0; i_tracks < max_track; ++i_tracks) {
      uint32_t i_sectors;
      struct disc_tool_sector* p_sectors;
      uint32_t num_sectors;
      uint8_t seen_sectors[256];
      uint32_t track_crc;

      if (is_fingerprinting) {
        track_crc = util_crc32_init();
      }

      disc_tool_set_track(p_tool, i_tracks);
      disc_tool_find_sectors(p_tool);
      p_sectors = disc_tool_get_sectors(p_tool, &num_sectors);

      if (log_protection) {
        if (p_tool->track_length >= (k_ibm_disc_bytes_per_track * 1.015)) {
          log_do_log(k_log_disc,
                     k_log_unusual,
                     "long track %d, %d bytes",
                     i_tracks,
                     p_tool->track_length);
        } else if (p_tool->track_length <
                   (k_ibm_disc_bytes_per_track * 0.985)) {
          log_do_log(k_log_disc,
                     k_log_unusual,
                     "short track %d, %d bytes",
                     i_tracks,
                     p_tool->track_length);
        }
        if (num_sectors == 0) {
          log_do_log(k_log_disc, k_log_info, "unformattted track %d", i_tracks);
        } else if (p_sectors->is_mfm) {
          if (num_sectors != 16) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "non-standard MFM sector count track %d count %d",
                       i_tracks,
                       num_sectors);
          }
        } else {
          if (num_sectors != 10) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "non-standard FM sector count track %d count %d",
                       i_tracks,
                       num_sectors);
          }
        }
      }

      (void) memset(seen_sectors, '\0', sizeof(seen_sectors));
      for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
        char sector_spec[14];
        uint8_t sector_data[k_disc_tool_max_sector_length + 1];
        uint8_t sector_track = p_sectors->header_bytes[0];
        uint8_t sector_head = p_sectors->header_bytes[1];
        uint8_t sector_sector = p_sectors->header_bytes[2];
        uint8_t sector_size = p_sectors->header_bytes[3];
        (void) snprintf(sector_spec,
                        sizeof(sector_spec),
                        "[%.2X %.2X %.2X %.2X]",
                        sector_track,
                        sector_head,
                        sector_sector,
                        sector_size);
        if (log_protection) {
          if (sector_track != i_tracks) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "track mismatch, track %d %s",
                       i_tracks,
                       sector_spec);
          }
          if ((sector_size > 0x07) ||
              ((128u << sector_size) != p_sectors->byte_length)) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "sector size mismatch, track %d %s (physical size %d)",
                       i_tracks,
                       sector_spec,
                       p_sectors->byte_length);
          }
          if (seen_sectors[sector_sector]) {
            log_do_log(k_log_disc,
                       k_log_unusual,
                       "duplicate logical sector, track %d logical sector %d",
                       i_tracks,
                       sector_sector);
          }
          if (p_sectors->is_deleted) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "deleted sector, track %d %s",
                       i_tracks,
                       sector_spec);
          }
          if ((p_sectors->is_mfm && (sector_sector > 15)) ||
              (!p_sectors->is_mfm && (sector_sector > 9))) {
            log_do_log(k_log_disc,
                       k_log_info,
                       "wild sector ID, track %d %s",
                       i_tracks,
                       sector_spec);
          }
        }
        seen_sectors[sector_sector] = 1;
        if (log_crc_errors || log_protection) {
          if (p_sectors->has_header_crc_error) {
            log_do_log(k_log_disc,
                       k_log_warning,
                       "header CRC error side %d track %d physical sector %d",
                       i_sides,
                       i_tracks,
                       i_sectors);
          }
          if (p_sectors->has_data_crc_error) {
            log_do_log(k_log_disc,
                       k_log_warning,
                       "data CRC error side %d track %d physical sector %d",
                       i_sides,
                       i_tracks,
                       i_sectors);
          }
        }
        if (is_fingerprinting || (p_raw_dump_file != NULL)) {
          disc_tool_read_sector(p_tool, NULL, &sector_data[0], i_sectors, 1);
        }
        if (is_fingerprinting) {
          int do_include_sector = 1;
          if (p_sectors->byte_length == 0) {
            do_include_sector = 0;
          }
          if (p_sectors->has_header_crc_error ||
              p_sectors->has_data_crc_error) {
            do_include_sector = 0;
          }
          /* 8271 has trouble reading certain logical tracks. */
          if (sector_track == 0xFF) {
             do_include_sector = 0;
          }
          if ((i_tracks != 0) && (sector_track == 0)) {
            do_include_sector = 0;
          }
          if (do_include_sector) {
            track_crc = util_crc32_add(track_crc,
                                       &sector_data[0],
                                       (p_sectors->byte_length + 1));
          }
        }
        if (p_raw_dump_file != NULL) {
          util_file_write(p_raw_dump_file,
                          &sector_data[1],
                          p_sectors->byte_length);
        }
        p_sectors++;
      }

      if (is_fingerprinting) {
        track_crc = util_crc32_finish(track_crc);
        if (log_fingerprint_tracks) {
          log_do_log(k_log_disc,
                     k_log_info,
                     "track %d CRC32 fingerprint %.8X",
                     i_tracks,
                     track_crc);
        }
        /* Full disc fingerprint only includes up to the 41st or 81st track. */
        if ((!is_80t && i_tracks <= 40) || (is_80t && i_tracks <= 80)) {
          /* NOTE: not endian safe. */
          disc_crc = util_crc32_add(disc_crc, (uint8_t*) &track_crc, 4);
          if (!(i_tracks & 1)) {
            disc_crc_even = util_crc32_add(disc_crc_even,
                                           (uint8_t*) &track_crc,
                                           4);
          }
        }
      }
    } /* End of tracks loop. */

    if (log_fingerprint) {
      disc_crc = util_crc32_finish(disc_crc);
      log_do_log(k_log_disc,
                 k_log_info,
                 "disc side %d CRC32 fingerprint %.8X",
                 i_sides,
                 disc_crc);
      if (is_80t) {
        disc_crc_even = util_crc32_finish(disc_crc_even);
        log_do_log(k_log_disc,
                   k_log_info,
                   "disc side %d, as 40 track, CRC32 fingerprint %.8X",
                   i_sides,
                   disc_crc_even);
      }
    }

  } /* End of sides loop. */

  if (p_raw_dump_file != NULL) {
    util_file_close(p_raw_dump_file);
  }

  disc_tool_destroy(p_tool);
}
