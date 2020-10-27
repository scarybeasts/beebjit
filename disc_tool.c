#include "disc_tool.h"

#include "ibm_disc_format.h"
#include "disc.h"
#include "disc_drive.h"
#include "util.h"

#include <string.h>

struct disc_tool_struct {
  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  uint32_t drive;
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
  p_tool->drive = drive;
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
  struct disc_drive_struct* p_drive;
  struct disc_struct* p_disc;
  int is_side_upper;
  uint32_t track = p_tool->track;

  if (p_tool->drive & 1) {
    p_drive = p_tool->p_drive_1;
  } else {
    p_drive = p_tool->p_drive_0;
  }

  p_disc = disc_drive_get_disc(p_drive);
  if (p_disc == NULL) {
    return NULL;
  }

  if (track >= k_ibm_disc_tracks_per_disc) {
    return NULL;
  }

  is_side_upper = !!(p_tool->drive & 2);

  return disc_get_raw_pulses_buffer(p_disc, is_side_upper, track);
}

void
disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                       uint8_t* p_buf,
                       uint32_t len) {
  uint32_t i;
  uint8_t clocks;
  uint8_t data;
  uint32_t pos = p_tool->pos;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    (void) memset(p_buf, '\0', len);
    return;
  }

  for (i = 0; i < len; ++i) {
    if (pos >= k_disc_max_bytes_per_track) {
      pos = 0;
    }
    ibm_disc_format_2us_pulses_to_fm(&clocks, &data, p_pulses[pos]);
    p_buf[i] = data;
    pos++;
  }

  p_tool->pos = pos;
}
