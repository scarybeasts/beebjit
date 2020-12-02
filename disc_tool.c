#include "disc_tool.h"

#include "ibm_disc_format.h"
#include "disc.h"
#include "log.h"
#include "util.h"

#include <stdio.h>

#include <assert.h>
#include <string.h>

enum {
  k_max_sectors = 32,
};

struct disc_tool_struct {
  struct disc_struct* p_disc;
  int is_side_upper;
  uint32_t track;
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
disc_tool_get_byte_pos(struct disc_tool_struct* p_tool) {
  return p_tool->pos;
}

void
disc_tool_set_disc(struct disc_tool_struct* p_tool,
                   struct disc_struct* p_disc) {
  p_tool->p_disc = p_disc;
  p_tool->num_sectors = 0;
}

void
disc_tool_set_is_side_upper(struct disc_tool_struct* p_tool,
                            int is_side_upper) {
  p_tool->is_side_upper = is_side_upper;
  p_tool->num_sectors = 0;
}

void
disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track) {
  p_tool->track = track;
  p_tool->num_sectors = 0;
}

void
disc_tool_set_byte_pos(struct disc_tool_struct* p_tool, uint32_t pos) {
  p_tool->pos = pos;
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

void
disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                       uint8_t* p_clocks,
                       uint8_t* p_data,
                       uint32_t len) {
  uint32_t i;
  uint32_t pos = p_tool->pos;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    (void) memset(p_clocks, '\0', len);
    (void) memset(p_data, '\0', len);
    return;
  }

  for (i = 0; i < len; ++i) {
    uint8_t clocks;
    uint8_t data;
    if (pos >= k_disc_max_bytes_per_track) {
      pos = 0;
    }
    ibm_disc_format_2us_pulses_to_fm(&clocks, &data, p_pulses[pos]);
    p_clocks[i] = clocks;
    p_data[i] = data;
    pos++;
  }

  p_tool->pos = pos;
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
                        uint8_t* p_data,
                        uint32_t len) {
  uint32_t i;
  uint32_t pos = p_tool->pos;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    return;
  }

  for (i = 0; i < len; ++i) {
    uint32_t pulses;
    if (pos >= k_disc_max_bytes_per_track) {
      pos = 0;
    }
    pulses = ibm_disc_format_fm_to_2us_pulses(0xFF, p_data[i]);
    p_pulses[pos] = pulses;
    pos++;
  }

  p_tool->pos = pos;

  disc_tool_commit_write(p_tool);
}

void
disc_tool_write_fm_data_with_clocks(struct disc_tool_struct* p_tool,
                                    uint8_t data,
                                    uint8_t clocks) {
  uint32_t pulses;
  uint32_t pos = p_tool->pos;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    return;
  }

  if (pos >= k_disc_max_bytes_per_track) {
    pos = 0;
  }
  pulses = ibm_disc_format_fm_to_2us_pulses(clocks, data);
  p_pulses[pos] = pulses;
  pos++;

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

void
disc_tool_find_sectors(struct disc_tool_struct* p_tool, int is_mfm) {
  uint32_t i;
  uint32_t track_length;
  uint32_t bit_length;
  uint32_t shift_register;
  uint32_t num_shifts;
  uint32_t pulses;
  uint16_t calculated_crc;
  uint32_t num_sectors = 0;
  uint64_t mark_detector = 0;
  int32_t header_byte = -1;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  struct disc_struct* p_disc = p_tool->p_disc;
  struct disc_tool_sector* p_sector = NULL;

  assert(!is_mfm);

  p_tool->num_sectors = 0;

  if (p_disc == NULL) {
    return;
  }

  track_length = disc_get_track_length(p_disc,
                                       p_tool->is_side_upper,
                                       p_tool->track);
  assert(track_length > 0);
  bit_length = (track_length * 32);
  shift_register = 0;
  num_shifts = 0;
  for (i = 0; i < bit_length; ++i) {
    uint8_t clocks;
    uint8_t data;

    if ((i & 31) == 0) {
      pulses = p_pulses[i / 32];
    }
    mark_detector <<= 1;
    shift_register <<= 1;
    num_shifts++;
    if (pulses & 0x80000000) {
      mark_detector |= 1;
      shift_register |= 1;
    }
    pulses <<= 1;

    if ((mark_detector & 0xFFFFFFFF00000000) == 0x8888888800000000) {
      ibm_disc_format_2us_pulses_to_fm(&clocks, &data, mark_detector);
      if (clocks == k_ibm_disc_mark_clock_pattern) {
        if (data == k_ibm_disc_id_mark_data_pattern) {
          if (num_sectors == k_max_sectors) {
            util_bail("too many sector headers");
          }
          p_sector = &p_tool->sectors[num_sectors];
          (void) memset(p_sector, '\0', sizeof(struct disc_tool_sector));
          num_sectors++;
          p_sector->bit_pos_header = i;
          shift_register = 0;
          num_shifts = 0;
          header_byte = 0;
          calculated_crc = ibm_disc_format_crc_init(0);
          calculated_crc = ibm_disc_format_crc_add_byte(calculated_crc, data);
        } else if ((data == k_ibm_disc_data_mark_data_pattern) ||
                   (data == k_ibm_disc_deleted_data_mark_data_pattern)) {
          if ((p_sector == NULL) || (p_sector->bit_pos_data != 0)) {
            log_do_log(k_log_disc,
                       k_log_unusual,
                       "sector data without header on track %d",
                       p_tool->track);
          } else {
            assert(p_sector->bit_pos_header != 0);
            p_sector->bit_pos_data = i;
            shift_register = 0;
            num_shifts = 0;
            header_byte = -1;
            calculated_crc = ibm_disc_format_crc_init(0);
            calculated_crc = ibm_disc_format_crc_add_byte(calculated_crc, data);
          }
        }
      }
    }

    if ((header_byte >= 0) && (num_shifts == 32)) {
      ibm_disc_format_2us_pulses_to_fm(&clocks, &data, shift_register);
      if (header_byte < 4) {
        calculated_crc = ibm_disc_format_crc_add_byte(calculated_crc, data);
      }
      switch (header_byte) {
      case 0:
        p_sector->sector_track = data;
        break;
      case 1:
        p_sector->sector_head = data;
        break;
      case 2:
        p_sector->sector_sector = data;
        break;
      case 3:
        p_sector->sector_size = data;
        break;
      case 4:
        p_sector->header_crc_on_disc = (data << 8);
        break;
      case 5:
        p_sector->header_crc_on_disc |= data;
        if (p_sector->header_crc_on_disc != calculated_crc) {
          p_sector->has_header_crc_error = 1;
        }
        break;
      }
      shift_register = 0;
      num_shifts = 0;
      header_byte++;
      if (header_byte == 6) {
        header_byte = -1;
      }
    }
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
disc_tool_log_summary(struct disc_struct* p_disc,
                      int log_crc_errors,
                      int log_protection) {
  uint32_t i_tracks;
  struct disc_tool_struct* p_tool = disc_tool_create();

  disc_tool_set_disc(p_tool, p_disc);
  for (i_tracks = 0; i_tracks < k_ibm_disc_tracks_per_disc; ++i_tracks) {
    uint32_t i_sectors;
    struct disc_tool_sector* p_sectors;
    uint32_t num_sectors;
    disc_tool_set_track(p_tool, i_tracks);
    disc_tool_find_sectors(p_tool, 0);
    p_sectors = disc_tool_get_sectors(p_tool, &num_sectors);
    for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
      if (log_crc_errors || log_protection) {
        if (p_sectors->has_header_crc_error) {
          log_do_log(k_log_disc,
                     k_log_warning,
                     "header CRC error track %d physical sector %d",
                     i_tracks,
                     i_sectors);
        }
        if (p_sectors->has_data_crc_error) {
          log_do_log(k_log_disc,
                     k_log_warning,
                     "data CRC error track %d physical sector %d",
                     i_tracks,
                     i_sectors);
        }
      }
      p_sectors++;
    }
  }

  disc_tool_destroy(p_tool);
}
