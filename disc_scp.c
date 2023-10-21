#include "disc_scp.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <string.h>

void
disc_scp_load(struct disc_struct* p_disc) {
  static const size_t k_max_scp_track_size = (1024 * 1024);
  uint32_t len;
  uint8_t header[16];
  uint8_t chunk[12];
  uint32_t i_tracks;
  uint32_t max_track;
  uint32_t num_tracks;
  uint32_t num_revs;
  uint32_t heads_byte;
  uint32_t num_sides;
  uint8_t scp_flags;
  uint8_t* p_scp_track_data;
  float* p_pulses;
  int is_one_side_only = 0;

  struct util_file* p_file = disc_get_file(p_disc);
  int is_skip_upper_side = disc_is_skip_upper_side(p_disc);
  int is_skip_odd_tracks = disc_is_skip_odd_tracks(p_disc);
  uint32_t rev = disc_get_required_rev(p_disc);

  assert(p_file != NULL);

  len = util_file_read(p_file, &header[0], sizeof(header));
  if (len != sizeof(header)) {
    util_bail("SCP missing header");
  }

  if (memcmp(&header[0], "SCP", 3) != 0) {
    util_bail("SCP bad header");
  }
  num_revs = header[5];
  if (num_revs == 0) {
    util_bail("SCP bad num revs");
  }
  if (header[6] != 0) {
    util_bail("SCP doesn't start at track 0");
  }
  max_track = header[7];
  if (max_track > 167) {
    util_bail("SCP excessive max track");
  }
  scp_flags = header[8];
  if (!(scp_flags & 1)) {
    util_bail("SCP not index cued");
  }
  if (header[9] != 0) {
    util_bail("SCP bad bitcell width");
  }
  heads_byte = header[10];
  if (heads_byte > 1) {
    util_bail("SCP heads byte %d not supported", heads_byte);
  }
  if (heads_byte == 0) {
    num_sides = 2;
  } else {
    num_sides = 1;
  }
  if (header[11] != 0) {
    util_bail("SCP resolution not 25ns");
  }

  num_tracks = (max_track + 1);

  log_do_log(k_log_disc,
             k_log_info,
             "SCP: loading %d sides, %d tracks, %d revs",
             num_sides,
             num_tracks,
             num_revs);

  p_scp_track_data = util_malloc(k_max_scp_track_size);
  p_pulses = util_malloc(k_max_scp_track_size / 2 * 4);

  for (i_tracks = 0; i_tracks < num_tracks; ++i_tracks) {
    uint32_t track_offset;
    uint32_t track_data_offset;
    uint32_t track_length;
    uint32_t actual_track;
    int side;
    uint32_t i_revs;

    util_file_seek(p_file, ((i_tracks * 4) + 16));
    len = util_file_read(p_file, &chunk[0], 4);
    if (len != 4) {
      util_bail("SCP can't read track meta offset");
    }
    track_offset = util_read_le32(&chunk[0]);
    if (track_offset == 0) {
      continue;
    }
    if ((num_sides == 1) && (i_tracks == 1)) {
      /* Well, this is awkward!
       * Normally, the track offsets are:
       * track0 / side0, track0 / side1, track1 / side0, ... and if the disc is
       * single sided, the "side1"s have an offset of 0.
       * However, some tools appear to emit dodgy SCPs that omit the "side1"s,
       * which I think could be counter to the spec? This is handled here.
       */
      is_one_side_only = 1;
      log_do_log(k_log_disc, k_log_info, "SCP: densely packed single side");
    }
    if (is_one_side_only) {
      actual_track = i_tracks;
      side = 0;
    } else {
      actual_track = (i_tracks / 2);
      side = (i_tracks & 1);
    }
    if (actual_track >= k_ibm_disc_tracks_per_disc) {
      util_bail("SCP excessive tracks");
    }

    if (is_skip_upper_side && (side == 1)) {
      continue;
    }
    if (is_skip_odd_tracks) {
      if (actual_track & 1) {
        continue;
      }
      actual_track /= 2;
    }

    util_file_seek(p_file, track_offset);
    len = util_file_read(p_file, &chunk[0], 4);
    if (len != 4) {
      util_bail("SCP can't read track header");
    }
    if (memcmp(&chunk[0], "TRK", 3) != 0) {
      util_bail("SCP bad track header");
    }
    if (chunk[3] != i_tracks) {
      util_bail("SCP track mismatch");
    }

    for (i_revs = 0; i_revs < num_revs; ++i_revs) {
      uint32_t i_data;
      uint32_t num_pulses;

      if (i_revs != rev) {
        continue;
      }

      util_file_seek(p_file, (track_offset + 4 + (i_revs * 12)));
      len = util_file_read(p_file, &chunk[0], 12);
      if (len != 12) {
        util_bail("SCP can't read rev meta");
      }
      track_data_offset = (track_offset + util_read_le32(&chunk[8]));
      track_length = util_read_le32(&chunk[4]);
      track_length *= 2;
      if (track_length > k_max_scp_track_size) {
        util_bail("SCP track too large");
      }

      util_file_seek(p_file, track_data_offset);
      len = util_file_read(p_file, p_scp_track_data, track_length);
      if (len != track_length) {
        util_bail("SCP can't read track data");
      }

      i_data = 0;
      num_pulses = 0;
      for (i_data = 0; i_data < track_length; i_data += 2) {
        uint16_t sample = util_read_be16(&p_scp_track_data[i_data]);
        float delta_us = (sample / 40.0);
        p_pulses[num_pulses] = delta_us;
        num_pulses++;
      }

      disc_build_track_from_pulses(p_disc,
                                   i_revs,
                                   side,
                                   actual_track,
                                   p_pulses,
                                   num_pulses);
    }
  }

  util_free(p_scp_track_data);
  util_free(p_pulses);
}
