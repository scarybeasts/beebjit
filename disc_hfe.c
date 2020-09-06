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
disc_hfe_encode_data(uint8_t* p_dest, uint8_t data, uint8_t clock) {
  uint8_t b0 = 0;
  uint8_t b1 = 0;
  uint8_t b2 = 0;
  uint8_t b3 = 0;

  if (data & 0x80) b0 |= 0x08;
  if (data & 0x40) b0 |= 0x80;
  if (data & 0x20) b1 |= 0x08;
  if (data & 0x10) b1 |= 0x80;
  if (data & 0x08) b2 |= 0x08;
  if (data & 0x04) b2 |= 0x80;
  if (data & 0x02) b3 |= 0x08;
  if (data & 0x01) b3 |= 0x80;
  if (clock & 0x80) b0 |= 0x02;
  if (clock & 0x40) b0 |= 0x20;
  if (clock & 0x20) b1 |= 0x02;
  if (clock & 0x10) b1 |= 0x20;
  if (clock & 0x08) b2 |= 0x02;
  if (clock & 0x04) b2 |= 0x20;
  if (clock & 0x02) b3 |= 0x02;
  if (clock & 0x01) b3 |= 0x20;

  p_dest[0] = b0;
  p_dest[1] = b1;
  p_dest[2] = b2;
  p_dest[3] = b3;
}

void
disc_hfe_write_track(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t length,
                     uint8_t* p_data,
                     uint8_t* p_clocks) {
  uint32_t hfe_track_offset;
  uint32_t hfe_track_len;
  uint32_t i_byte;
  uint32_t metadata_index;
  uint8_t buffer[(k_disc_max_bytes_per_track * 4) + 3];
  uint8_t hfe_chunk[256];

  struct util_file* p_file = disc_get_file(p_disc);
  uint8_t* p_metadata = disc_get_format_metadata(p_disc);
  uint8_t version = p_metadata[k_hfe_format_metadata_offset_version];
  uint32_t buffer_index = 0;
  uint32_t write_pos = 0;

  assert(p_file != NULL);

  if (version == 3) {
    buffer[0] = disc_hfe_byte_flip(k_hfe_v3_opcode_setindex);
    buffer[1] = disc_hfe_byte_flip(k_hfe_v3_opcode_setbitrate);
    buffer[2] = disc_hfe_byte_flip(72);
    buffer_index += 3;
  }
  for (i_byte = 0; i_byte < length; ++i_byte) {
    uint8_t data = p_data[i_byte];
    uint8_t clocks = p_clocks[i_byte];
    if ((version == 3) && (data == 0) && (clocks == 0)) {
      /* Mark weak bits explicitly in HFEv3. */
      uint8_t byte = disc_hfe_byte_flip(k_hfe_v3_opcode_rand);
      (void) memset(&buffer[buffer_index], byte, 4);
    } else {
      disc_hfe_encode_data(&buffer[buffer_index],
                           p_data[i_byte],
                           p_clocks[i_byte]);
    }
    buffer_index += 4;
  }

  metadata_index = (track * 4);
  hfe_track_offset = (p_metadata[metadata_index] +
                      (p_metadata[metadata_index + 1] << 8));
  hfe_track_offset *= 512;
  hfe_track_len = (p_metadata[metadata_index + 2] +
                   (p_metadata[metadata_index + 3] << 8));

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
    if (write_pos >= hfe_track_len) {
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
  uint8_t* p_lut;
  uint8_t* p_metadata;

  struct util_file* p_file = disc_get_file(p_disc);
  int is_double_sided = 0;
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
  if (p_file_buf[11] != 2) {
    if (p_file_buf[11] == 0xFF) {
      log_do_log(k_log_disc, k_log_warning, "unknown encoding, trying anyway");
    } else {
      util_bail("hfe encoding not ISOIBM_FM_ENCODING: %d",
                (int) p_file_buf[11]);
    }
  }
  if (p_file_buf[10] == 1) {
    is_double_sided = 0;
  } else if (p_file_buf[10] == 2) {
    is_double_sided = 1;
  } else {
    util_bail("hfe invalid number of sides: %d", (int) p_file_buf[10]);
  }
  disc_set_is_double_sided(p_disc, is_double_sided);

  hfe_tracks = p_file_buf[9];
  if (hfe_tracks > k_ibm_disc_tracks_per_disc) {
    util_bail("hfe excessive tracks: %d", (int) hfe_tracks);
  }
  if (expand_to_80 && ((hfe_tracks * 2) <= k_ibm_disc_tracks_per_disc)) {
    expand_multiplier = 2;
  }

  lut_offset = (p_file_buf[18] + (p_file_buf[19] << 8));
  lut_offset *= 512;

  if ((lut_offset + 512) > file_len) {
    util_bail("hfe LUT doesn't fit");
  }

  (void) memcpy(p_metadata, (p_file_buf + lut_offset), 512);
  p_lut = p_metadata;

  for (i_track = 0; i_track < hfe_tracks; ++i_track) {
    uint32_t track_offset;
    uint32_t hfe_track_len;
    uint8_t* p_track_lut;
    uint8_t* p_track_data;
    uint32_t i_byte;
    uint32_t i_side;

    p_track_lut = (p_lut + (i_track * 4));
    track_offset = (p_track_lut[0] + (p_track_lut[1] << 8));
    track_offset *= 512;
    hfe_track_len = (p_track_lut[2] + (p_track_lut[3] << 8));

    if ((track_offset + hfe_track_len) > file_len) {
      util_bail("hfe track doesn't fit");
    }

    p_track_data = (p_file_buf + track_offset);

    for (i_side = 0; i_side < 2; ++i_side) {
      uint8_t* p_data;
      uint8_t* p_clocks;

      uint32_t bytes_written = 0;
      uint32_t buf_len = (hfe_track_len / 2);
      int is_setbitrate = 0;
      int is_skipbits = 0;
      uint32_t skipbits_length = 0;
      uint8_t data = 0;
      uint8_t clocks = 0;
      uint32_t shift_counter = 0;
      uint32_t bit_counter = 0;
      int bit = 0;

      p_data = disc_get_raw_track_data(p_disc,
                                       i_side,
                                       (i_track * expand_multiplier));
      p_clocks = disc_get_raw_track_clocks(p_disc,
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
                       "HFE v3 SETBITRATE wild (72==250kbit): %d",
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
                         "HFE v3 SETINDEX not at byte 0: %d",
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
          bit |= ((byte & 0x80) != 0);
          byte <<= 1;
          bit_counter++;
          if (bit_counter == 1) {
            continue;
          }
          bit_counter = 0;
          if (!(shift_counter & 1)) {
            clocks <<= 1;
            clocks |= bit;
          } else {
            data <<= 1;
            data |= bit;
          }
          bit = 0;
          shift_counter++;
          if (shift_counter != 16) {
            continue;
          }
          /* Single-sided HFEs seem to repeat side 0 data on side 1, so remove
           * it.
           */
          if (!is_double_sided && (i_side == 1)) {
            data = 0;
            clocks = 0;
          }
          p_data[bytes_written] = data;
          p_clocks[bytes_written] = clocks;
          bytes_written++;
          clocks = 0;
          data = 0;
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
disc_hfe_convert(struct disc_struct* p_disc) {
  uint32_t i_track;
  uint8_t header[512];
  uint8_t* p_metadata;

  /* 4 bytes per data byte, 3 "header" HFEv3 bytes, 2 sides. */
  uint32_t hfe_track_len = (((k_ibm_disc_bytes_per_track * 4) + 3) * 2);
  uint32_t hfe_offset = 2;
  uint32_t hfe_offset_delta = ((hfe_track_len / 512) + 1);
  struct util_file* p_file = disc_get_file(p_disc);
  int is_double_sided = disc_is_double_sided(p_disc);

  /* Fill with 0xFF; that is what the command line HFE tools do, and also, 0xFF
   * appears to be the byte used for the default / sane boolean option.
   */
  (void) memset(header, '\xFF', sizeof(header));
  (void) strcpy((char*) header, k_hfe_header_v3);
  /* Revision 0. */
  header[8] = 0;
  if (disc_is_double_sided(p_disc)) {
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

  for (i_track = 0; i_track < k_ibm_disc_tracks_per_disc; ++i_track) {
    uint8_t* p_data;
    uint8_t* p_clocks;
    uint32_t index = (i_track * 4);
    uint32_t track_length = disc_get_track_length(p_disc, 0, i_track);

    if (track_length == 0) {
      /* Stop when we hit uninitialized tracks. */
      break;
    }
    assert(track_length == k_ibm_disc_bytes_per_track);

    p_metadata[index] = (hfe_offset & 0xFF);
    p_metadata[index + 1] = (hfe_offset >> 8);
    p_metadata[index + 2] = (hfe_track_len & 0xFF);
    p_metadata[index + 3] = (hfe_track_len >> 8);

    p_data = disc_get_raw_track_data(p_disc, 0, i_track);
    p_clocks = disc_get_raw_track_clocks(p_disc, 0, i_track);
    disc_hfe_write_track(p_disc,
                         0,
                         i_track,
                         k_ibm_disc_bytes_per_track,
                         p_data,
                         p_clocks);
    if (is_double_sided) {
      p_data = disc_get_raw_track_data(p_disc, 1, i_track);
      p_clocks = disc_get_raw_track_clocks(p_disc, 1, i_track);
      disc_hfe_write_track(p_disc,
                           1,
                           i_track,
                           k_ibm_disc_bytes_per_track,
                           p_data,
                           p_clocks);
    }

    hfe_offset += hfe_offset_delta;
  }

  /* Number of valid tracks is now known so fill it in. */
  header[9] = i_track;

  util_file_seek(p_file, 0);
  util_file_write(p_file, header, 512);
  util_file_seek(p_file, 512);
  util_file_write(p_file, p_metadata, 512);
  util_file_flush(p_file);
}
