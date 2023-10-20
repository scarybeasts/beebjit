#include "disc_hfe.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "util.h"

#include <assert.h>
#include <string.h>

static const char* k_hfe_header_v1 = "HXCPICFE";
static const char* k_hfe_header_v3 = "HXCHFEV3";
static uint32_t k_hfe_format_metadata_size = 513;
static uint32_t k_hfe_format_metadata_offset_version = 512;
static uint8_t k_hfe_v3_opcode_mask = 0xF0;
static uint8_t k_hfe_v3_opcode_mask_flipped = 0x0F;
enum {
  k_hfe_v3_opcode_nop = 0xF0,
  k_hfe_v3_opcode_setindex = 0xF1,
  k_hfe_v3_opcode_setbitrate = 0xF2,
  k_hfe_v3_opcode_skipbits = 0xF3,
  k_hfe_v3_opcode_rand = 0xF4,
};

static uint8_t
disc_hfe_byte_flip(uint8_t val) {
  uint8_t ret = 0;

  /* This could be a table but it's not performance critical. */
  if (val & 0x80) ret |= 0x01;
  if (val & 0x40) ret |= 0x02;
  if (val & 0x20) ret |= 0x04;
  if (val & 0x10) ret |= 0x08;
  if (val & 0x08) ret |= 0x10;
  if (val & 0x04) ret |= 0x20;
  if (val & 0x02) ret |= 0x40;
  if (val & 0x01) ret |= 0x80;

  return ret;
}

static void
disc_hfe_encode_data(uint8_t* p_dest, uint32_t pulses) {
  uint32_t i;

  for (i = 0; i < 4; ++i) {
    uint8_t byte = (pulses >> 24);
    byte = disc_hfe_byte_flip(byte);
    p_dest[i] = byte;
    pulses <<= 8;
  }
}

static void
disc_hfe_get_track_offset_and_length(struct disc_struct* p_disc,
                                     uint32_t* p_offset,
                                     uint32_t* p_length,
                                     uint32_t track) {
  uint8_t* p_metadata = disc_get_format_metadata(p_disc);
  uint32_t metadata_index = (track * 4);
  uint32_t hfe_track_offset = (p_metadata[metadata_index] +
                                   (p_metadata[metadata_index + 1] << 8));
  uint32_t hfe_track_length = (p_metadata[metadata_index + 2] +
                                   (p_metadata[metadata_index + 3] << 8));
  hfe_track_offset *= 512;

  *p_offset = hfe_track_offset;
  *p_length = hfe_track_length;
}

static void
disc_hfe_zero_track_in_file(struct disc_struct* p_disc, uint32_t track) {
  uint32_t hfe_track_offset;
  uint32_t hfe_track_length;
  uint32_t written = 0;
  uint8_t zero_chunk[512];

  struct util_file* p_file = disc_get_file(p_disc);

  (void) memset(zero_chunk, '\0', sizeof(zero_chunk));


  disc_hfe_get_track_offset_and_length(p_disc,
                                       &hfe_track_offset,
                                       &hfe_track_length,
                                       track);

  util_file_seek(p_file, hfe_track_offset);

  while (written < hfe_track_length) {
    util_file_write(p_file, zero_chunk, 512);
    written += 512;
  }
}

void
disc_hfe_write_track(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t length,
                     uint32_t* p_pulses) {
  uint32_t hfe_track_offset;
  uint32_t hfe_track_length;
  uint32_t i_byte;
  uint8_t buffer[(k_disc_max_bytes_per_track * 4) + 3];
  uint8_t hfe_chunk[256];

  struct util_file* p_file = disc_get_file(p_disc);
  uint8_t* p_metadata = disc_get_format_metadata(p_disc);
  uint8_t version = p_metadata[k_hfe_format_metadata_offset_version];
  uint32_t buffer_index = 0;
  uint32_t write_pos = 0;
  uint32_t num_tracks = disc_get_num_tracks_used(p_disc);

  assert(p_file != NULL);

  disc_hfe_get_track_offset_and_length(p_disc,
                                       &hfe_track_offset,
                                       &hfe_track_length,
                                       track);
  if (hfe_track_offset < 1024) {
    log_do_log(k_log_disc,
               k_log_error,
               "track %d unallocated in HFE, ignoring write",
               track);
    return;
  }

  /* This track write might in fact have extended the HFE file so make sure the
   * track count in the header is kept up to date.
   */
  if (track >= num_tracks) {
    uint8_t new_num_tracks = (uint8_t) (track + 1);
    util_file_seek(p_file, 9);
    util_file_write(p_file, &new_num_tracks, 1);

    disc_hfe_zero_track_in_file(p_disc, track);
  }

  if (version == 3) {
    buffer[0] = disc_hfe_byte_flip(k_hfe_v3_opcode_setindex);
    buffer[1] = disc_hfe_byte_flip(k_hfe_v3_opcode_setbitrate);
    buffer[2] = disc_hfe_byte_flip(72);
    buffer_index += 3;
  }
  for (i_byte = 0; i_byte < length; ++i_byte) {
    uint32_t pulses = p_pulses[i_byte];
    if (version == 3) {
      if (pulses == 0) {
        /* Mark weak bits explicitly in HFEv3. */
        uint8_t byte = disc_hfe_byte_flip(k_hfe_v3_opcode_rand);
        (void) memset(&buffer[buffer_index], byte, 4);
      } else {
        uint32_t i_check_invalid;
        disc_hfe_encode_data(&buffer[buffer_index], pulses);
        /* This is ugly, but certain pulse stream values are invalid in HFEv3
         * because they are used for stream opcodes.
         * We might accidentally emit a stream opcode, if our internal pulse
         * stream was built from a crazy stream of 2us pulses. This can happen
         * in an SCP on an unformatted track.
         * So detect any, and replace with the random opcode.
         */
        for (i_check_invalid = 0; i_check_invalid < 4; ++i_check_invalid) {
          uint8_t byte = buffer[buffer_index + i_check_invalid];
          if ((byte & k_hfe_v3_opcode_mask_flipped) ==
              k_hfe_v3_opcode_mask_flipped) {
            buffer[buffer_index + i_check_invalid] =
                disc_hfe_byte_flip(k_hfe_v3_opcode_rand);
          }
        }
      }
    } else {
      disc_hfe_encode_data(&buffer[buffer_index], pulses);
    }
    buffer_index += 4;
  }

  i_byte = 0;
  write_pos = 0;
  if (is_side_upper) {
    write_pos = 256;
  }

  while (i_byte < buffer_index) {
    uint32_t chunk_len = 256;
    uint32_t read_left = (buffer_index - i_byte);
    if (read_left < 256) {
      chunk_len = read_left;
      (void) memset(hfe_chunk, '\0', 256);
    }

    (void) memcpy(hfe_chunk, &buffer[i_byte], chunk_len);

    util_file_seek(p_file, (hfe_track_offset + write_pos));
    util_file_write(p_file, hfe_chunk, 256);
    write_pos += 512;
    if (write_pos >= hfe_track_length) {
      break;
    }

    i_byte += chunk_len;
  }
}

void
disc_hfe_load(struct disc_struct* p_disc, int expand_to_80) {
  /* HFE (v1?):
   * https://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf
   */
  static const size_t k_max_hfe_size = (1024 * 1024 * 4);
  uint8_t* p_file_buf;
  uint32_t file_len;
  uint32_t hfe_tracks;
  uint32_t i_track;
  uint32_t lut_offset;
  uint8_t* p_metadata;

  struct util_file* p_file = disc_get_file(p_disc);
  uint32_t num_sides = 1;
  int is_v3 = 0;
  uint32_t expand_multiplier = 1;

  assert(p_file != NULL);

  p_file_buf = util_malloc(k_max_hfe_size);

  p_metadata = disc_allocate_format_metadata(p_disc,
                                             k_hfe_format_metadata_size);

  file_len = util_file_read(p_file, p_file_buf, k_max_hfe_size);

  if (file_len == k_max_hfe_size) {
    util_bail("hfe file too large");
  }

  if (file_len < 512) {
    util_bail("hfe file no header");
  }
  if (memcmp(p_file_buf, k_hfe_header_v1, 8) == 0) {
    /* HFE v1. */
    p_metadata[k_hfe_format_metadata_offset_version] = 1;
  } else if (memcmp(p_file_buf, k_hfe_header_v3, 8) == 0) {
    /* HFE v3. */
    is_v3 = 1;
    p_metadata[k_hfe_format_metadata_offset_version] = 3;
  } else {
    util_bail("hfe file incorrect header");
  }
  if (p_file_buf[8] != '\0') {
    util_bail("hfe file revision not 0");
  }
  if ((p_file_buf[11] != 2) && (p_file_buf[11] != 0)) {
    if (p_file_buf[11] == 0xFF) {
      log_do_log(k_log_disc,
                 k_log_warning,
                 "unknown encoding %d, trying anyway",
                 p_file_buf[11]);
    } else {
      util_bail("hfe encoding not ISOIBM_(M)FM_ENCODING: %d",
                (int) p_file_buf[11]);
    }
  }
  if (p_file_buf[10] == 1) {
    /* Leave num_sides at 1. */
  } else if (p_file_buf[10] == 2) {
    num_sides = 2;
  } else {
    util_bail("hfe invalid number of sides: %d", (int) p_file_buf[10]);
  }

  hfe_tracks = p_file_buf[9];
  if (hfe_tracks > k_ibm_disc_tracks_per_disc) {
    util_bail("hfe excessive tracks: %d", (int) hfe_tracks);
  }
  if (expand_to_80 && ((hfe_tracks * 2) <= k_ibm_disc_tracks_per_disc)) {
    expand_multiplier = 2;
    log_do_log(k_log_disc, k_log_info, "HFE: expanding 40 to 80");
  }

  log_do_log(k_log_disc,
             k_log_info,
             "HFE: v%d loading %d sides, %d tracks",
             p_metadata[k_hfe_format_metadata_offset_version],
             num_sides,
             hfe_tracks);

  lut_offset = (p_file_buf[18] + (p_file_buf[19] << 8));
  lut_offset *= 512;

  if ((lut_offset + 512) > file_len) {
    util_bail("hfe LUT doesn't fit");
  }

  (void) memcpy(p_metadata, (p_file_buf + lut_offset), 512);

  for (i_track = 0; i_track < hfe_tracks; ++i_track) {
    uint32_t hfe_track_offset;
    uint32_t hfe_track_length;
    uint8_t* p_track_data;
    uint32_t i_byte;
    uint32_t i_side;

    disc_hfe_get_track_offset_and_length(p_disc,
                                         &hfe_track_offset,
                                         &hfe_track_length,
                                         i_track);

    if ((hfe_track_offset + hfe_track_length) > file_len) {
      util_bail("hfe track %d doesn't fit (length %d offset %d file length %d)",
                i_track,
                hfe_track_length,
                hfe_track_offset,
                file_len);
    }

    p_track_data = (p_file_buf + hfe_track_offset);

    for (i_side = 0; i_side < num_sides; ++i_side) {
      uint32_t* p_pulses;

      uint32_t bytes_written = 0;
      uint32_t buf_len = (hfe_track_length / 2);
      int is_setbitrate = 0;
      int is_skipbits = 0;
      uint32_t skipbits_length = 0;
      uint32_t shift_counter = 0;
      uint32_t pulses = 0;

      p_pulses = disc_get_raw_pulses_buffer(p_disc,
                                            i_side,
                                            (i_track * expand_multiplier));

      for (i_byte = 0; i_byte < buf_len; ++i_byte) {
        uint32_t i;
        uint32_t index;
        uint8_t byte;

        uint32_t num_bits = 8;

        if (bytes_written == k_disc_max_bytes_per_track) {
          log_do_log(k_log_disc,
                     k_log_warning,
                     "HFE track %d truncated",
                     i_track);
          break;
        }

        index = (i_byte / 256);
        index *= 512;
        if (i_side == 1) {
          index += 256;
        }
        index += (i_byte % 256);

        byte = p_track_data[index];
        byte = disc_hfe_byte_flip(byte);

        if (is_setbitrate) {
          is_setbitrate = 0;
          if ((byte < 64) || (byte > 80)) {
            log_do_log(k_log_disc,
                       k_log_warning,
                       "HFE v3 SETBITRATE wild (72==250kbit) track %d: %d",
                       i_track,
                       (int) byte);
          }
          continue;
        } else if (is_skipbits) {
          is_skipbits = 0;
          if ((byte == 0) || (byte >= 8)) {
            util_bail("HFE v3 invalid skipbits %d", (int) byte);
          }
          skipbits_length = byte;
          continue;
        } else if (skipbits_length) {
          byte <<= (8 - skipbits_length);
          num_bits = skipbits_length;
          skipbits_length = 0;
        } else if (is_v3 &&
                   ((byte & k_hfe_v3_opcode_mask) == k_hfe_v3_opcode_mask)) {
          switch (byte) {
          case k_hfe_v3_opcode_nop:
            continue;
          case k_hfe_v3_opcode_setindex:
            if (bytes_written != 0) {
              log_do_log(k_log_disc,
                         k_log_warning,
                         "HFE v3 SETINDEX not at byte 0, track %d: %d",
                         i_track,
                         (int) bytes_written);
            }
            continue;
          case k_hfe_v3_opcode_setbitrate:
            is_setbitrate = 1;
            continue;
          case k_hfe_v3_opcode_rand:
            /* Internally we represent weak bits on disc as a no flux area. */
            byte = 0;
            break;
          case k_hfe_v3_opcode_skipbits:
            is_skipbits = 1;
            continue;
          default:
            util_bail("HFE v3 unknown opcode 0x%X", (int) byte);
            break;
          }
        }

        for (i = 0; i < num_bits; ++i) {
          pulses <<= 1;
          pulses |= !!(byte & 0x80);
          byte <<= 1;
          shift_counter++;
          if (shift_counter != 32) {
            continue;
          }
          p_pulses[bytes_written] = pulses;
          bytes_written++;
          pulses = 0;
          shift_counter = 0;
        }
      }
      disc_set_track_length(p_disc,
                            i_side,
                            (i_track * expand_multiplier),
                            bytes_written);
    }
  }

  util_free(p_file_buf);
}

void
disc_hfe_create_header(struct disc_struct* p_disc) {
  uint32_t i_track;
  uint8_t header[512];
  uint8_t* p_metadata;

  /* 4 bytes per data byte, 3 "header" HFEv3 bytes, 2 sides. */
  uint32_t hfe_track_len = (((k_ibm_disc_bytes_per_track * 4) + 3) * 2);
  uint32_t hfe_offset = 2;
  uint32_t hfe_offset_delta = ((hfe_track_len / 512) + 1);
  struct util_file* p_file = disc_get_file(p_disc);
  int is_double_sided = disc_is_double_sided(p_disc);
  uint32_t num_tracks = disc_get_num_tracks_used(p_disc);

  /* Fill with 0xFF; that is what the command line HFE tools do, and also, 0xFF
   * appears to be the byte used for the default / sane boolean option.
   */
  (void) memset(header, '\xFF', sizeof(header));
  (void) strcpy((char*) header, k_hfe_header_v3);
  /* Revision 0. */
  header[8] = 0;
  header[9] = num_tracks;
  if (is_double_sided) {
    header[10] = 2;
  } else {
    header[10] = 1;
  }
  /* IBM FM, 250kbit, (unused) RPM. */
  header[11] = 2;
  header[12] = 0xFA;
  header[13] = 0;
  header[14] = 0;
  header[15] = 0;
  /* Mode: Shuggart DD. Unused. 1==512 LUT offset. */
  header[16] = 7;
  header[17] = 0xFF;
  header[18] = 1;
  header[19] = 0;
  /* Write allowed, single step, no alternate track options. */
  header[20] = 0xFF;
  header[21] = 0xFF;
  header[22] = 0xFF;
  header[23] = 0xFF;
  header[24] = 0xFF;
  header[25] = 0xFF;

  p_metadata = disc_allocate_format_metadata(p_disc,
                                             k_hfe_format_metadata_size);
  /* HFE v3. */
  p_metadata[k_hfe_format_metadata_offset_version] = 3;

  for (i_track = 0; i_track < num_tracks; ++i_track) {
    uint32_t index = (i_track * 4);

    p_metadata[index] = (hfe_offset & 0xFF);
    p_metadata[index + 1] = (hfe_offset >> 8);
    p_metadata[index + 2] = (hfe_track_len & 0xFF);
    p_metadata[index + 3] = (hfe_track_len >> 8);

    disc_hfe_zero_track_in_file(p_disc, i_track);

    hfe_offset += hfe_offset_delta;
  }

  util_file_seek(p_file, 0);
  util_file_write(p_file, header, 512);
  util_file_seek(p_file, 512);
  util_file_write(p_file, p_metadata, 512);
  util_file_flush(p_file);
}
