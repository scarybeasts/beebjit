#include "disc.h"

#include "bbc_options.h"
#include "disc_adl.h"
#include "disc_dfi.h"
#include "disc_fsd.h"
#include "disc_hfe.h"
#include "disc_kryo.h"
#include "disc_rfi.h"
#include "disc_scp.h"
#include "disc_ssd.h"
#include "disc_tool.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <math.h>
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
  /* Options. */
  int log_protection;
  int log_iffy_pulses;
  int expand_to_80;
  int is_quantize_fm;
  int is_skip_odd_tracks;
  int is_skip_upper_side;
  uint32_t rev;
  char rev_spec[256];

  char* p_file_name;
  struct util_file* p_file;
  uint8_t* p_format_metadata;
  void (*p_write_track_callback)(struct disc_struct* p_disc,
                                 int is_side_upper,
                                 uint32_t track,
                                 uint32_t length,
                                 uint32_t* p_pulses);

  /* State of the disc. */
  int is_ssd;
  int is_dsd;
  int is_adl;
  int is_fsd;
  int is_log;
  int is_rfi;
  int is_raw;
  int is_scp;
  int is_dfi;
  int is_hfe;
  struct disc_side lower_side;
  struct disc_side upper_side;
  int is_double_sided;
  uint32_t tracks_used;
  int is_writeable;
  int is_mutable_requested;
  int is_mutable;
  int had_first_load;

  int is_dirty;
  int32_t dirty_side;
  int32_t dirty_track;

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
  p_disc->is_double_sided = 0;
}

static void
disc_do_convert(struct disc_struct* p_disc,
                int do_convert_to_hfe,
                int do_convert_to_ssd,
                int do_convert_to_adl) {
  const char* p_file_name;
  int do_write_all_tracks;

  if (!do_convert_to_hfe && !do_convert_to_ssd && !do_convert_to_adl) {
    return;
  }

  disc_load(p_disc);

  p_file_name = p_disc->p_file_name;
  do_write_all_tracks = 0;

  if (do_convert_to_hfe && !p_disc->is_hfe) {
    char new_file_name[4096];
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.hfe",
                    p_file_name);
    log_do_log(k_log_disc, k_log_info, "converting to HFE: %s", new_file_name);
    util_file_close(p_disc->p_file);
    p_disc->p_file = util_file_open(new_file_name, 1, 1);
    disc_hfe_create_header(p_disc);
    p_disc->p_write_track_callback = disc_hfe_write_track;
    do_write_all_tracks = 1;
  } else if (do_convert_to_ssd && !p_disc->is_ssd && !p_disc->is_dsd) {
    char new_file_name[4096];
    int is_double_sided = disc_is_double_sided(p_disc);
    const char* p_suffix = (is_double_sided ? "dsd" : "ssd");
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.%s",
                    p_file_name,
                    p_suffix);
    log_do_log(k_log_disc, k_log_info, "converting to SSD: %s", new_file_name);
    util_file_close(p_disc->p_file);
    p_disc->p_file = util_file_open(new_file_name, 1, 1);
    p_disc->p_write_track_callback = disc_ssd_write_track;
    do_write_all_tracks = 1;
  } else if (do_convert_to_adl && !p_disc->is_adl) {
    char new_file_name[4096];
    (void) snprintf(new_file_name,
                    sizeof(new_file_name),
                    "%s.adl",
                    p_file_name);
    log_do_log(k_log_disc, k_log_info, "converting to ADL: %s", new_file_name);
    util_file_close(p_disc->p_file);
    p_disc->p_file = util_file_open(new_file_name, 1, 1);
    p_disc->p_write_track_callback = disc_adl_write_track;
    do_write_all_tracks = 1;
  }
  if (do_write_all_tracks) {
    uint32_t i_track;
    uint32_t num_tracks = disc_get_num_tracks_used(p_disc);
    int is_double_sided = disc_is_double_sided(p_disc);
    p_disc->is_mutable = 1;
    for (i_track = 0; i_track < num_tracks; ++i_track) {
      disc_dirty_and_flush(p_disc, 0, i_track);
      if (is_double_sided) {
        disc_dirty_and_flush(p_disc, 1, i_track);
      }
    }
  }
}

struct disc_struct*
disc_create(const char* p_file_name,
            int is_writeable,
            int is_mutable,
            int do_convert_to_hfe,
            int do_convert_to_ssd,
            int do_convert_to_adl,
            struct bbc_options* p_options) {
  int do_fingerprint;
  int do_fingerprint_tracks;
  int do_log_catalog;
  int do_dump_sector_data;
  int do_check_for_crc_errors = 0;
  char* p_rev_spec = NULL;

  struct disc_struct* p_disc = util_mallocz(sizeof(struct disc_struct));

  p_disc->log_protection = util_has_option(p_options->p_log_flags,
                                           "disc:protection");
  p_disc->log_iffy_pulses = util_has_option(p_options->p_log_flags,
                                            "disc:iffy-pulses");
  do_fingerprint = util_has_option(p_options->p_log_flags, "disc:fingerprint");
  do_fingerprint_tracks = util_has_option(p_options->p_log_flags,
                                          "disc:track-fingerprint");
  do_log_catalog = util_has_option(p_options->p_log_flags, "disc:catalog");
  do_dump_sector_data = util_has_option(p_options->p_opt_flags,
                                        "disc:dump-sector-data");
  p_disc->expand_to_80 = util_has_option(p_options->p_opt_flags,
                                         "disc:expand-to-80");
  p_disc->is_quantize_fm = util_has_option(p_options->p_opt_flags,
                                           "disc:quantize-fm");
  p_disc->is_skip_odd_tracks = util_has_option(p_options->p_opt_flags,
                                               "disc:skip-odd-tracks");
  p_disc->is_skip_upper_side = util_has_option(p_options->p_opt_flags,
                                               "disc:skip-upper-side");
  p_disc->rev = 0;
  (void) util_get_u32_option(&p_disc->rev, p_options->p_opt_flags, "disc:rev=");
  (void) util_get_str_option(&p_rev_spec,
                             p_options->p_opt_flags,
                             "disc:rev-spec=");
  if (p_rev_spec != NULL) {
    (void) snprintf(&p_disc->rev_spec[0],
                    sizeof(p_disc->rev_spec),
                    "%s",
                    p_rev_spec);
    util_free(p_rev_spec);
  }
  p_disc->p_file_name = util_strdup(p_file_name);
  p_disc->p_file = NULL;
  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;
  p_disc->tracks_used = 0;
  p_disc->is_double_sided = 0;

  if (util_is_extension(p_file_name, "ssd")) {
    p_disc->is_ssd = 1;
    p_disc->p_write_track_callback = disc_ssd_write_track;
  } else if (util_is_extension(p_file_name, "dsd")) {
    p_disc->is_dsd = 1;
    p_disc->p_write_track_callback = disc_ssd_write_track;
  } else if (util_is_extension(p_file_name, "adl")) {
    p_disc->is_adl = 1;
    p_disc->p_write_track_callback = disc_adl_write_track;
  } else if (util_is_extension(p_file_name, "fsd")) {
    p_disc->is_fsd = 1;
  } else if (util_is_extension(p_file_name, "log")) {
    p_disc->is_log = 1;
  } else if (util_is_extension(p_file_name, "rfi")) {
    p_disc->is_rfi = 1;
  } else if (util_is_extension(p_file_name, "raw")) {
    p_disc->is_raw = 1;
  } else if (util_is_extension(p_file_name, "scp")) {
    p_disc->is_scp = 1;
  } else if (util_is_extension(p_file_name, "dfi")) {
    p_disc->is_dfi = 1;
  } else if (util_is_extension(p_file_name, "hfe")) {
    p_disc->is_hfe = 1;
    p_disc->p_write_track_callback = disc_hfe_write_track;
  } else {
    util_bail("unknown disc filename extension");
  }

  if (is_mutable && (p_disc->p_write_track_callback == NULL)) {
    log_do_log(k_log_disc,
               k_log_warning,
               "cannot writeback to file type, making read only");
    is_writeable = 0;
    is_mutable = 0;
  }

  p_disc->is_writeable = is_writeable;
  p_disc->is_mutable_requested = is_mutable;
  /* Mutable gets set by disc_load(). */
  p_disc->is_mutable = 0;

  /* The raw flux formats are often "hot off the press" from a Greaseweazle or
   * similar device, so always check for CRC errors.
   */
  if (p_disc->is_rfi || p_disc->is_raw || p_disc->is_scp || p_disc->is_dfi) {
    do_check_for_crc_errors = 1;
  }

  if (do_check_for_crc_errors ||
      p_disc->log_protection ||
      do_fingerprint ||
      do_fingerprint_tracks ||
      do_log_catalog ||
      do_dump_sector_data) {
    disc_load(p_disc);
    disc_tool_log_summary(p_disc,
                          do_check_for_crc_errors,
                          p_disc->log_protection,
                          do_fingerprint,
                          do_fingerprint_tracks,
                          do_log_catalog,
                          do_dump_sector_data);
  }

  disc_do_convert(p_disc,
                  do_convert_to_hfe,
                  do_convert_to_ssd,
                  do_convert_to_adl);

  return p_disc;
}

void
disc_load(struct disc_struct* p_disc) {
  const char* p_file_name;
  int is_file_writeable;

  assert(!p_disc->is_dirty);

  if (p_disc->had_first_load && !p_disc->is_mutable_requested) {
    return;
  }
  p_disc->had_first_load = 1;

  p_file_name = p_disc->p_file_name;
  is_file_writeable = 0;

  if (p_disc->p_format_metadata != NULL) {
    util_free(p_disc->p_format_metadata);
    p_disc->p_format_metadata = NULL;
  }
  if (p_disc->p_file != NULL) {
    util_file_close(p_disc->p_file);
    p_disc->p_file = NULL;
  }

  disc_init_surface(p_disc, 0x00);

  if (p_disc->is_mutable_requested) {
    is_file_writeable = 1;
  }

  p_disc->p_file = util_file_try_open(p_file_name, is_file_writeable, 0);
  if ((p_disc->p_file == NULL) && is_file_writeable) {
    log_do_log(k_log_disc,
               k_log_warning,
               "file %s is not writable, making disc read only",
               p_file_name);
    p_disc->p_file = util_file_open(p_file_name, 0, 0);

    is_file_writeable = 0;
    p_disc->is_writeable = 0;
  }
  if (p_disc->p_file == NULL) {
    util_bail("couldn't open %s", p_file_name);
  }

  p_disc->is_mutable = is_file_writeable;

  if (p_disc->is_ssd) {
    disc_ssd_load(p_disc, 0);
  } else if (p_disc->is_dsd) {
    disc_ssd_load(p_disc, 1);
  } else if (p_disc->is_adl) {
    disc_adl_load(p_disc);
  } else if (p_disc->is_fsd) {
    disc_fsd_load(p_disc, 1);
  } else if (p_disc->is_log) {
    disc_fsd_load(p_disc, 0);
  } else if (p_disc->is_rfi) {
    disc_rfi_load(p_disc);
  } else if (p_disc->is_raw) {
    disc_kryo_load(p_disc, p_file_name);
  } else if (p_disc->is_scp) {
    disc_scp_load(p_disc);
  } else if (p_disc->is_dfi) {
    disc_dfi_load(p_disc);
  } else if (p_disc->is_hfe) {
    disc_hfe_load(p_disc, p_disc->expand_to_80);
  }
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
  p_disc->is_mutable_requested = 1;

  p_disc->is_dirty = 0;
  p_disc->dirty_side = -1;
  p_disc->dirty_track = -1;

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

  p_disc->p_write_track_callback = disc_hfe_write_track;
  disc_hfe_create_header(p_disc);
  disc_dirty_and_flush(p_disc, 0, 0);

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
disc_set_track_used(struct disc_struct* p_disc,
                    int is_side_upper,
                    uint32_t track) {
  if (is_side_upper) {
    p_disc->is_double_sided = 1;
  }
  if ((track + 1) > p_disc->tracks_used) {
    p_disc->tracks_used = (track + 1);
  }
}

int
disc_is_double_sided(struct disc_struct* p_disc) {
  return p_disc->is_double_sided;
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
disc_dirty_and_flush(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track) {
  p_disc->is_dirty = 1;
  p_disc->dirty_side = is_side_upper;
  p_disc->dirty_track = track;

  disc_flush_writes(p_disc);
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
  disc_set_track_used(p_disc, is_side_upper, track);
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

  disc_set_track_used(p_disc, is_side_upper, track);
}

static void
disc_put_fm_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks) {
  struct disc_track* p_track = p_disc->p_track;
  uint32_t pulses = ibm_disc_format_fm_to_2us_pulses(clocks, data);

  assert(p_disc->build_index < k_ibm_disc_bytes_per_track);

  p_track->pulses2us[p_disc->build_index] = pulses;
  p_disc->build_index++;
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

static int
disc_build_append_pulse_delta(struct disc_struct* p_disc, float delta_us) {
  uint32_t num_2us_units;
  if (!p_disc->is_quantize_fm) {
    num_2us_units = roundf(delta_us / 2.0);
  } else {
    num_2us_units = roundf(delta_us / 4.0);
    num_2us_units *= 2;
  }

  while (num_2us_units--) {
    if (p_disc->build_index == k_disc_max_bytes_per_track) {
      return 0;
    }
    if (num_2us_units == 0) {
      uint32_t val = (0x80000000 >> p_disc->build_pulses_index);
      assert(p_disc->build_index < k_disc_max_bytes_per_track);
      p_disc->p_track->pulses2us[p_disc->build_index] |= val;
    }
    p_disc->build_pulses_index++;
    if (p_disc->build_pulses_index == 32) {
      p_disc->build_pulses_index = 0;
      p_disc->build_index++;
    }
  }
  return 1;
}

void
disc_build_track_from_pulses(struct disc_struct* p_disc,
                             uint32_t rev,
                             int is_side_upper,
                             uint32_t track,
                             float* p_pulse_deltas,
                             uint32_t num_pulses) {
  uint32_t i_pulses;
  int did_truncation_warning;

  if (p_disc->rev != rev) {
    return;
  }
  if (p_disc->is_skip_upper_side && is_side_upper) {
    return;
  }
  if (p_disc->is_skip_odd_tracks) {
    if (track & 1) {
      return;
    }
    track /= 2;
  }

  disc_build_track(p_disc, is_side_upper, track);

  did_truncation_warning = 0;
  for (i_pulses = 0; i_pulses < num_pulses; ++i_pulses) {
    float delta = p_pulse_deltas[i_pulses];
    if (p_disc->log_iffy_pulses) {
      if (!ibm_disc_format_check_pulse(delta, !p_disc->is_quantize_fm)) {
        log_do_log(k_log_disc,
                   k_log_info,
                   "side %d track %d pulse %d iffy pulse %f (%s)",
                   is_side_upper,
                   track,
                   i_pulses,
                   delta,
                   (p_disc->is_quantize_fm ? "fm" : "mfm"));
      }
    }
    if (!disc_build_append_pulse_delta(p_disc, delta) &&
        !did_truncation_warning) {
      did_truncation_warning = 1;
      log_do_log(k_log_disc,
                 k_log_warning,
                 "pulse loader truncating side %d track %d",
                 is_side_upper,
                 track);
    }
  }

  disc_build_set_track_length(p_disc);
}

void
disc_build_set_track_length(struct disc_struct* p_disc) {
  uint32_t build_index = p_disc->build_index;
  assert(build_index <= k_disc_max_bytes_per_track);
  if (build_index != 0) {
    p_disc->p_track->length = build_index;
  }
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
disc_set_track_length(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track,
                      uint32_t length) {
  struct disc_track* p_track = disc_get_track(p_disc, is_side_upper, track);

  assert(length <= k_disc_max_bytes_per_track);
  assert(length > 0);
  p_track->length = length;

  disc_set_track_used(p_disc, is_side_upper, track);
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
