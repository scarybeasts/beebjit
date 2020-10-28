#include "disc_tool.h"

#include "ibm_disc_format.h"
#include "disc.h"
#include "disc_drive.h"
#include "util.h"

#include <string.h>

struct disc_tool_struct {
  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  struct disc_drive_struct* p_drive;
  int is_side_upper;
  uint32_t track;
  uint32_t pos;
};

struct disc_tool_struct*
disc_tool_create(struct disc_drive_struct* p_drive_0,
                 struct disc_drive_struct* p_drive_1) {
  struct disc_tool_struct* p_tool =
      util_mallocz(sizeof(struct disc_tool_struct));
  p_tool->p_drive_0 = p_drive_0;
  p_tool->p_drive_1 = p_drive_1;
  p_tool->p_drive = p_drive_0;

  return p_tool;
}

void
disc_tool_destroy(struct disc_tool_struct* p_tool) {
  util_free(p_tool);
}

uint32_t
disc_tool_get_pos(struct disc_tool_struct* p_tool) {
  return p_tool->pos;
}

void
disc_tool_set_drive(struct disc_tool_struct* p_tool, uint32_t drive) {
  if (drive & 1) {
    p_tool->p_drive = p_tool->p_drive_1;
  } else {
    p_tool->p_drive = p_tool->p_drive_0;
  }

  p_tool->is_side_upper = !!(drive & 2);
}

void
disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track) {
  p_tool->track = track;
}

void
disc_tool_set_pos(struct disc_tool_struct* p_tool, uint32_t pos) {
  p_tool->pos = pos;
}

static uint32_t*
disc_tool_get_pulses(struct disc_tool_struct* p_tool) {
  struct disc_struct* p_disc;
  struct disc_drive_struct* p_drive = p_tool->p_drive;
  int is_side_upper = p_tool->is_side_upper;
  uint32_t track = p_tool->track;

  p_disc = disc_drive_get_disc(p_drive);
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
  struct disc_struct* p_disc = disc_drive_get_disc(p_tool->p_drive);
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
