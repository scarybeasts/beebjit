#include "disc.h"

#include "bbc_options.h"
#include "disc_adl.h"
#include "disc_fsd.h"
#include "disc_hfe.h"
#include "disc_ssd.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct disc_track {
  uint32_t length;
  uint32_t pulses2us[k_disc_max_bytes_per_track];
};

struct disc_side {
  struct disc_track tracks[k_ibm_disc_tracks_per_disc];
};

struct disc_struct {
  int log_protection;
  int expand_to_80;

  char* p_file_name;
  struct util_file* p_file;
  uint8_t* p_format_metadata;
  int is_mutable;
  void (*p_write_track_callback)(struct disc_struct* p_disc,
                                 int is_side_upper,
                                 uint32_t track,
                                 uint32_t length,
                                 uint32_t* p_pulses);

  /* State of the disc. */
  struct disc_side lower_side;
  struct disc_side upper_side;
  int is_double_sided;
  int is_writeable;

  int is_dirty;
  int32_t dirty_side;
  int32_t dirty_track;
  uint32_t tracks_used;

  /* Track building. */
  struct disc_track* p_track;
  uint32_t build_index;
  uint32_t build_pulses_index;
  int build_last_mfm_bit;
  uint16_t crc;
};

static void
disc_init_surface(struct disc_struct* p_disc, uint8_t byte) {
  uint32_t i;
  for (i = 0; i < k_ibm_disc_tracks_per_disc; ++i) {
    p_disc->lower_side.tracks[i].length = k_ibm_disc_bytes_per_track;
    (void) memset(&p_disc->lower_side.tracks[i].pulses2us,
                  byte,
                  sizeof(p_disc->lower_side.tracks[i].pulses2us));
    p_disc->upper_side.tracks[i].length = k_ibm_disc_bytes_per_track;
    (void) memset(&p_disc->upper_side.tracks[i].pulses2us,
                  byte,
                  sizeof(p_disc->upper_side.tracks[i].pulses2us));
  }

  p_disc->tracks_used = 0;
}

struct disc_struct*
disc_create(const char* p_file_name,
            int is_writeable,
            int is_mutable,
            int convert_to_hfe,
            struct bbc_options* p_options) {
  int is_file_writeable = 0;
  int is_hfe = 0;

  struct disc_struct* p_disc = util_mallocz(sizeof(struct disc_struct));
  disc_init_surface(p_disc, 0x00);

  p_disc->log_protection = util_has_option(p_options->p_log_flags,
                                           "disc:protection");
  p_disc->expand_to_80 = util_has_option(p_options->p_opt_flags,
                                         "disc:expand-to-80");
  p_disc->p_file_name = util_strdup(p_file_name);
  p_disc->p_file = NULL;
  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;
  p_disc->tracks_used = 0;

  if (is_mutable) {
    is_file_writeable = 1;
  }
  p_disc->p_file = util_file_open(p_file_name, is_file_writeable, 0);

  if (util_is_extension(p_file_name, "ssd")) {
    disc_ssd_load(p_disc, 0);
    p_disc->p_write_track_callback = disc_ssd_write_track;
  } else if (util_is_extension(p_file_name, "dsd")) {
    disc_ssd_load(p_disc, 1);
    p_disc->p_write_track_callback = disc_ssd_write_track;
  } else if (util_is_extension(p_file_name, "adl")) {
    disc_adl_load(p_disc);
    p_disc->p_write_track_callback = disc_adl_write_track;
  } else if (util_is_extension(p_file_name, "fsd")) {
    disc_fsd_load(p_disc, 1, p_disc->log_protection);
  } else if (util_is_extension(p_file_name, "log")) {
    disc_fsd_load(p_disc, 0, p_disc->log_protection);
  } else if (util_is_extension(p_file_name, "hfe")) {
    disc_hfe_load(p_disc, p_disc->expand_to_80);
    p_disc->p_write_track_callback = disc_hfe_write_track;
    is_hfe = 1;
  } else {
    util_bail("unknown disc filename extension");
  }

  if (is_mutable && (p_disc->p_write_track_callback == NULL)) {
    log_do_log(k_log_disc, k_log_info, "cannot writeback to file type");
    convert_to_hfe = 1;
  }
  if (convert_to_hfe && !is_hfe) {
    char new_file_name[4096];
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.hfe",
                    p_file_name);
    log_do_log(k_log_disc, k_log_info, "converting to HFE: %s", new_file_name);
    util_file_close(p_disc->p_file);
    p_disc->p_file = util_file_open(new_file_name, 1, 1);
    disc_hfe_convert(p_disc);
    p_disc->p_write_track_callback = disc_hfe_write_track;
  }

  p_disc->is_writeable = is_writeable;
  p_disc->is_mutable = is_mutable;

  return p_disc;
}

struct disc_struct*
disc_create_from_raw(const char* p_file_name, const char* p_raw_spec) {
  size_t len;
  size_t spec_pos;
  uint32_t track_pos;

  struct disc_struct* p_disc = util_mallocz(sizeof(struct disc_struct));
  /* For now, fill unused space with 1 bits, as opposed to empty disc surface,
   * to get deterministic behavior.
   */
  disc_init_surface(p_disc, 0xFF);

  p_disc->p_file_name = util_strdup(p_file_name);
  p_disc->p_file = util_file_open(p_file_name, 1, 1);
  p_disc->is_writeable = 1;
  p_disc->is_mutable = 1;

  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;
  p_disc->tracks_used = 0;

  /* Fill disc bytes from the raw spec. */
  len = strlen(p_raw_spec);
  spec_pos = 0;
  track_pos = 0;

  disc_build_track(p_disc, 0, 0);

  while (spec_pos < len) {
    uint8_t data;
    uint8_t clocks;

    /* Allow a . as a no-op to aid with clarity. */
    if (p_raw_spec[spec_pos] == '.') {
      spec_pos++;
      continue;
    }

    if ((spec_pos + 4) > len) {
      util_bail("malformed disc spec");
    }

    if (track_pos == k_ibm_disc_bytes_per_track) {
      util_bail("disc spec too big for track");
    }
    data = util_parse_hex2(p_raw_spec + spec_pos);
    clocks = util_parse_hex2(p_raw_spec + spec_pos + 2);

    disc_build_append_fm_data_and_clocks(p_disc, data, clocks);

    spec_pos += 4;
  }

  disc_hfe_convert(p_disc);
  p_disc->p_write_track_callback = disc_hfe_write_track;

  return p_disc;
}

void
disc_destroy(struct disc_struct* p_disc) {
  assert(!p_disc->is_dirty);
  if (p_disc->p_format_metadata != NULL) {
    util_free(p_disc->p_format_metadata);
  }
  if (p_disc->p_file != NULL) {
    util_file_close(p_disc->p_file);
  }
  util_free(p_disc->p_file_name);
  util_free(p_disc);
}

static void
disc_set_track_used(struct disc_struct* p_disc, uint32_t track) {
  if ((track + 1) > p_disc->tracks_used) {
    p_disc->tracks_used = (track + 1);
  }
}

uint32_t
disc_get_num_tracks_used(struct disc_struct* p_disc) {
  return p_disc->tracks_used;
}

void
disc_write_pulses(struct disc_struct* p_disc,
                  int is_side_upper,
                  uint32_t track,
                  uint32_t pos,
                  uint32_t pulses) {
  uint32_t* p_pulses = disc_get_raw_pulses_buffer(p_disc, is_side_upper, track);

  assert(pos < disc_get_track_length(p_disc, is_side_upper, track));

  if (p_disc->is_dirty) {
    assert(is_side_upper == p_disc->dirty_side);
    assert(track == (uint32_t) p_disc->dirty_track);
  }

  p_disc->is_dirty = 1;
  p_disc->dirty_side = is_side_upper;
  p_disc->dirty_track = track;

  p_pulses[pos] = pulses;
}

void
disc_flush_writes(struct disc_struct* p_disc) {
  uint32_t* p_pulses;
  uint32_t length;

  int is_side_upper = p_disc->dirty_side;
  int32_t track = p_disc->dirty_track;

  if (!p_disc->is_dirty) {
    assert(is_side_upper == -1);
    assert(track == -1);
    return;
  }

  assert(is_side_upper != -1);
  assert(track != -1);

  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;

  if (!p_disc->is_mutable) {
    return;
  }

  p_pulses = disc_get_raw_pulses_buffer(p_disc, is_side_upper, track);
  length = disc_get_track_length(p_disc, is_side_upper, track);
  p_disc->p_write_track_callback(p_disc,
                                 is_side_upper,
                                 track,
                                 length,
                                 p_pulses);
  util_file_flush(p_disc->p_file);

  /* Mark the track used after the write track callback, so that the file
   * handler can tell if this was a file extension or not.
   */
  disc_set_track_used(p_disc, track);
}

static struct disc_track*
disc_get_track(struct disc_struct* p_disc, int is_side_upper, uint32_t track) {
  struct disc_track* p_track;

  if (is_side_upper) {
    p_track = &p_disc->upper_side.tracks[track];
  } else {
    p_track = &p_disc->lower_side.tracks[track];
  }

  return p_track;
}

void
disc_build_track(struct disc_struct* p_disc,
                 int is_side_upper,
                 uint32_t track) {
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);

  p_disc->p_track = p_track;
  p_track->length = k_ibm_disc_bytes_per_track;
  p_disc->build_index = 0;
  p_disc->build_pulses_index = 0;
  p_disc->build_last_mfm_bit = 0;

  disc_set_track_used(p_disc, track);
}

static void
disc_put_fm_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
  struct disc_track* p_track = p_disc->p_track;
  uint32_t pulses = ibm_disc_format_fm_to_2us_pulses(clocks, data);

  p_track->pulses2us[p_disc->build_index] = pulses;
  p_disc->build_index++;
  assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);
}

void
disc_build_reset_crc(struct disc_struct* p_disc) {
  p_disc->crc = ibm_disc_format_crc_init(0);
}

void
disc_build_append_fm_data_and_clocks(struct disc_struct* p_disc,
                                     uint8_t data,
                                     uint8_t clocks) {
  disc_put_fm_byte(p_disc, data, clocks);
  p_disc->crc = ibm_disc_format_crc_add_byte(p_disc->crc, data);
}

void
disc_build_append_fm_byte(struct disc_struct* p_disc, uint8_t data) {
  disc_build_append_fm_data_and_clocks(p_disc, data, 0xFF);
}

void
disc_build_append_repeat_fm_byte(struct disc_struct* p_disc,
                                 uint8_t data,
                                 size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_fm_byte(p_disc, data);
  }
}

void
disc_build_append_repeat_fm_byte_with_clocks(struct disc_struct* p_disc,
                                             uint8_t data,
                                             uint8_t clocks,
                                             size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_fm_data_and_clocks(p_disc, data, clocks);
  }
}

void
disc_build_append_fm_chunk(struct disc_struct* p_disc,
                           uint8_t* p_src,
                           size_t num) {
  size_t i;

  for (i = 0; i < num; ++i) {
    disc_build_append_fm_byte(p_disc, p_src[i]);
  }
}

static void
disc_build_append_mfm_pulses(struct disc_struct* p_disc, uint16_t pulses) {
  uint32_t merged_pulses;
  struct disc_track* p_track = p_disc->p_track;

  merged_pulses = p_track->pulses2us[p_disc->build_index];
  if (p_disc->build_pulses_index == 0) {
    p_disc->build_pulses_index = 16;
    merged_pulses &= 0x0000FFFF;
    merged_pulses |= (pulses << 16);
  } else {
    p_disc->build_pulses_index = 0;
    merged_pulses &= 0xFFFF0000;
    merged_pulses |= pulses;
  }
  p_track->pulses2us[p_disc->build_index] = merged_pulses;

  if (p_disc->build_pulses_index == 0) {
    p_disc->build_index++;
    assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);
  }
}

void
disc_build_append_mfm_byte(struct disc_struct* p_disc, uint8_t data) {
  uint16_t pulses =
      ibm_disc_format_mfm_to_2us_pulses(&p_disc->build_last_mfm_bit, data);

  disc_build_append_mfm_pulses(p_disc, pulses);

  p_disc->crc = ibm_disc_format_crc_add_byte(p_disc->crc, data);
}

void
disc_build_append_repeat_mfm_byte(struct disc_struct* p_disc,
                                  uint8_t data,
                                  uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    disc_build_append_mfm_byte(p_disc, data);
  }
}

void
disc_build_append_mfm_3x_A1_sync(struct disc_struct* p_disc) {
  uint32_t i;
  for (i = 0; i < 3; ++i) {
    disc_build_append_mfm_pulses(p_disc, k_ibm_disc_mfm_a1_sync);
    p_disc->crc = ibm_disc_format_crc_add_byte(p_disc->crc, 0xA1);
  }
}

void
disc_build_append_mfm_chunk(struct disc_struct* p_disc,
                            uint8_t* p_src,
                            uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    disc_build_append_mfm_byte(p_disc, p_src[i]);
  }
}

void
disc_build_fill_mfm_byte(struct disc_struct* p_disc, uint8_t data) {
  assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);

  while (p_disc->build_index < k_ibm_disc_bytes_per_track) {
    disc_build_append_mfm_byte(p_disc, data);
  }
}

void
disc_build_append_crc(struct disc_struct* p_disc, int is_mfm) {
  uint16_t crc = p_disc->crc;
  uint8_t first_byte = (crc >> 8);
  uint8_t second_byte = (crc & 0xFF);

  if (is_mfm) {
    disc_build_append_mfm_byte(p_disc, first_byte);
    disc_build_append_mfm_byte(p_disc, second_byte);
  } else {
    disc_build_append_fm_byte(p_disc, first_byte);
    disc_build_append_fm_byte(p_disc, second_byte);
  }
}

void
disc_build_append_bad_crc(struct disc_struct* p_disc) {
  /* Cache the crc because the calls below will corrupt it. */
  uint16_t crc = 0xFFFF;
  if (p_disc->crc == 0xFFFF) {
    crc = 0xFFFE;
  }

  disc_build_append_fm_byte(p_disc, (crc >> 8));
  disc_build_append_fm_byte(p_disc, (crc & 0xFF));
}

void
disc_build_fill_fm_byte(struct disc_struct* p_disc, uint8_t data) {
  uint32_t build_index = p_disc->build_index;
  assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);
  disc_build_append_repeat_fm_byte(p_disc,
                                   data,
                                   (k_ibm_disc_bytes_per_track - build_index));
}

const char*
disc_get_file_name(struct disc_struct* p_disc) {
  return p_disc->p_file_name;
}

struct util_file*
disc_get_file(struct disc_struct* p_disc) {
  return p_disc->p_file;
}

uint8_t*
disc_allocate_format_metadata(struct disc_struct* p_disc, size_t num_bytes) {
  uint8_t* p_format_metadata;

  assert(p_disc->p_format_metadata == NULL);
  p_format_metadata = util_mallocz(num_bytes);
  p_disc->p_format_metadata = p_format_metadata;

  return p_format_metadata;
}

void
disc_set_is_double_sided(struct disc_struct* p_disc, int is_double_sided) {
  p_disc->is_double_sided = is_double_sided;
}

void
disc_set_track_length(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track,
                      uint32_t length) {
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);

  assert(length <= k_disc_max_bytes_per_track);
  assert(length > 0);
  p_track->length = length;

  disc_set_track_used(p_disc, track);
}

int
disc_is_write_protected(struct disc_struct* p_disc) {
  return !p_disc->is_writeable;
}

uint8_t*
disc_get_format_metadata(struct disc_struct* p_disc) {
  return p_disc->p_format_metadata;
}

uint32_t
disc_get_track_length(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track) {
  uint32_t length;
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);
  length = p_track->length;
  assert(length > 0);
  return length;
}

uint32_t
disc_read_pulses(struct disc_struct* p_disc,
                 int is_side_upper,
                 uint32_t track,
                 uint32_t pos) {
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);

  assert(pos < p_track->length);

  return p_track->pulses2us[pos];
}

uint32_t*
disc_get_raw_pulses_buffer(struct disc_struct* p_disc,
                           int is_side_upper,
                           uint32_t track) {
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);
  return &p_track->pulses2us[0];
}

int
disc_is_double_sided(struct disc_struct* p_disc) {
  return p_disc->is_double_sided;
}
