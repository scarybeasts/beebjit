#include "disc.h"

#include "bbc_options.h"
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
  uint8_t data[k_disc_max_bytes_per_track];
  uint8_t clocks[k_disc_max_bytes_per_track];
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
                                 uint8_t* p_data,
                                 uint8_t* p_clocks);

  /* State of the disc. */
  struct disc_side lower_side;
  struct disc_side upper_side;
  int is_double_sided;
  int is_writeable;

  int is_dirty;
  int32_t dirty_side;
  int32_t dirty_track;

  /* Track building. */
  struct disc_track* p_track;
  uint32_t build_index;
  uint16_t crc;
};

struct disc_struct*
disc_create(const char* p_file_name,
            int is_writeable,
            int is_mutable,
            int convert_to_hfe,
            struct bbc_options* p_options) {
  int is_file_writeable = 0;
  int is_hfe = 0;
  struct disc_struct* p_disc = util_mallocz(sizeof(struct disc_struct));

  p_disc->log_protection = util_has_option(p_options->p_log_flags,
                                           "disc:protection");
  p_disc->expand_to_80 = util_has_option(p_options->p_opt_flags,
                                         "disc:expand-to-80");
  p_disc->p_file_name = util_strdup(p_file_name);
  p_disc->p_file = NULL;
  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;

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
  (void) memset(&p_disc->lower_side, '\xff', sizeof(p_disc->lower_side));
  (void) memset(&p_disc->upper_side, '\xff', sizeof(p_disc->upper_side));

  p_disc->p_file_name = util_strdup(p_file_name);
  p_disc->p_file = util_file_open(p_file_name, 1, 1);
  p_disc->is_writeable = 1;
  p_disc->is_mutable = 1;

  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;

  /* Fill disc bytes from the raw spec. */
  len = strlen(p_raw_spec);
  spec_pos = 0;
  track_pos = 0;
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

    /* TODO: use disc_build APIs. */
    p_disc->lower_side.tracks[0].data[track_pos] = data;
    p_disc->lower_side.tracks[0].clocks[track_pos] = clocks;

    track_pos++;
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

void
disc_write_byte(struct disc_struct* p_disc,
                int is_side_upper,
                uint32_t track,
                uint32_t pos,
                uint8_t data,
                uint8_t clocks) {
  uint8_t* p_data = disc_get_raw_track_data(p_disc, is_side_upper, track);
  uint8_t* p_clocks = disc_get_raw_track_clocks(p_disc, is_side_upper, track);

  if (p_disc->is_dirty) {
    assert(is_side_upper == p_disc->dirty_side);
    assert(track == (uint32_t) p_disc->dirty_track);
  }

  p_disc->is_dirty = 1;
  p_disc->dirty_side = is_side_upper;
  p_disc->dirty_track = track;

  p_data[pos] = data;
  p_clocks[pos] = clocks;
}

void
disc_flush_writes(struct disc_struct* p_disc) {
  uint8_t* p_data;
  uint8_t* p_clocks;
  uint32_t length;

  int is_side_upper = p_disc->dirty_side;
  int32_t track = p_disc->dirty_track;

  if (!p_disc->is_dirty) {
    assert(is_side_upper == -1);
    assert(track == -1);
    return;
  }

  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;

  if (!p_disc->is_mutable) {
    return;
  }

  p_data = disc_get_raw_track_data(p_disc, is_side_upper, track);
  p_clocks = disc_get_raw_track_clocks(p_disc, is_side_upper, track);
  length = disc_get_track_length(p_disc, is_side_upper, track);
  p_disc->p_write_track_callback(p_disc,
                                 is_side_upper,
                                 track,
                                 length,
                                 p_data,
                                 p_clocks);
  util_file_flush(p_disc->p_file);
}


void
disc_build_track(struct disc_struct* p_disc,
                 int is_side_upper,
                 uint32_t track) {
  struct disc_track* p_track;

  assert(!p_disc->is_dirty);
  if (is_side_upper) {
    p_track = &p_disc->upper_side.tracks[track];
  } else {
    p_track = &p_disc->lower_side.tracks[track];
  }
  p_disc->p_track = p_track;
  p_track->length = k_ibm_disc_bytes_per_track;
  p_disc->build_index = 0;
}

static void
disc_put_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
  struct disc_track* p_track = p_disc->p_track;

  p_track->data[p_disc->build_index] = data;
  p_track->clocks[p_disc->build_index] = clocks;
  p_disc->build_index++;
  assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);
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

void
disc_build_fill(struct disc_struct* p_disc, uint8_t data) {
  uint32_t build_index = p_disc->build_index;
  assert(p_disc->build_index <= k_ibm_disc_bytes_per_track);
  disc_build_append_repeat(p_disc,
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
  struct disc_track* p_track;
  if (is_side_upper) {
    p_track = &p_disc->upper_side.tracks[track];
  } else {
    p_track = &p_disc->lower_side.tracks[track];
  }

  assert(p_track->length == 0);
  assert(length <= k_disc_max_bytes_per_track);
  assert(length > 0);
  p_track->length = length;
}

int
disc_is_write_protected(struct disc_struct* p_disc) {
  return !p_disc->is_writeable;
}

uint8_t*
disc_get_format_metadata(struct disc_struct* p_disc) {
  return p_disc->p_format_metadata;
}

uint8_t*
disc_get_raw_track_data(struct disc_struct* p_disc,
                        int is_side_upper,
                        uint32_t track) {
  if (is_side_upper) {
    return &p_disc->upper_side.tracks[track].data[0];
  } else {
    return &p_disc->lower_side.tracks[track].data[0];
  }
}

uint8_t*
disc_get_raw_track_clocks(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track) {
  if (is_side_upper) {
    return &p_disc->upper_side.tracks[track].clocks[0];
  } else {
    return &p_disc->lower_side.tracks[track].clocks[0];
  }
}

uint32_t
disc_get_track_length(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track) {
  struct disc_track* p_track;
  if (is_side_upper) {
    p_track = &p_disc->upper_side.tracks[track];
  } else {
    p_track = &p_disc->lower_side.tracks[track];
  }

  return p_track->length;
}

int
disc_is_double_sided(struct disc_struct* p_disc) {
  return p_disc->is_double_sided;
}
