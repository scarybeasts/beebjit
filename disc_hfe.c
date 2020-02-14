#include "disc_hfe.h"

#include "disc.h"
#include "ibm_disc_format.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <string.h>

static void
disc_hfe_extract_data(uint8_t* p_data, uint8_t* p_clock, uint8_t* p_src) {
  uint8_t data = 0;
  uint8_t clock = 0;

  uint8_t b0 = p_src[0];
  uint8_t b1 = p_src[1];
  uint8_t b2 = p_src[2];
  uint8_t b3 = p_src[3];

  if (b0 & 0x08) data |= 0x80;
  if (b0 & 0x80) data |= 0x40;
  if (b1 & 0x08) data |= 0x20;
  if (b1 & 0x80) data |= 0x10;
  if (b2 & 0x08) data |= 0x08;
  if (b2 & 0x80) data |= 0x04;
  if (b3 & 0x08) data |= 0x02;
  if (b3 & 0x80) data |= 0x01;
  if (b0 & 0x02) clock |= 0x80;
  if (b0 & 0x20) clock |= 0x40;
  if (b1 & 0x02) clock |= 0x20;
  if (b1 & 0x20) clock |= 0x10;
  if (b2 & 0x02) clock |= 0x08;
  if (b2 & 0x20) clock |= 0x04;
  if (b3 & 0x02) clock |= 0x02;
  if (b3 & 0x20) clock |= 0x01;


  *p_data = data;
  *p_clock = clock;
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
disc_hfe_write_track(struct disc_struct* p_disc) {
  uint32_t hfe_track_offset;
  uint32_t hfe_track_len;
  uint8_t* p_metadata;
  uint32_t write_len;
  uint32_t i_byte;
  uint8_t* p_src_data;
  uint8_t* p_src_clocks;
  uint8_t buf[64 * 4];

  intptr_t file_handle = disc_get_file_handle(p_disc);
  uint32_t track = disc_get_track(p_disc);

  assert(file_handle != k_util_file_no_handle);
  assert(disc_is_track_dirty(p_disc));

  p_metadata = disc_get_format_metadata(p_disc);
  p_metadata += (track * 4);
  hfe_track_offset = (p_metadata[0] + (p_metadata[1] << 8));
  hfe_track_offset *= 512;
  if (disc_is_upper_side(p_disc)) {
    hfe_track_offset += 256;
  }
  hfe_track_len = (p_metadata[2] + (p_metadata[3] << 8));
  hfe_track_len /= 4;

  p_src_data = disc_get_raw_track_data(p_disc);
  p_src_clocks = disc_get_raw_track_clocks(p_disc);

  write_len = k_ibm_disc_bytes_per_track;
  if (write_len > hfe_track_len) {
    write_len = hfe_track_len;
  }

  for (i_byte = 0; i_byte < write_len; ++i_byte) {
    uint32_t index = (i_byte % 64);

    disc_hfe_encode_data((buf + (index * 4)),
                         p_src_data[i_byte],
                         p_src_clocks[i_byte]);

    if ((index == 63) || (i_byte == (write_len - 1))) {
      util_file_handle_seek(file_handle, hfe_track_offset);
      util_file_handle_write(file_handle, buf, ((index + 1) * 4));
      hfe_track_offset += 512;
    }
  }
}

void
disc_hfe_load(struct disc_struct* p_disc) {
  /* HFE (v1?):
   * https://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf
   */
  static const size_t k_max_hfe_size = (1024 * 1024 * 4);
  uint8_t buf[k_max_hfe_size];
  uint32_t file_len;
  uint32_t hfe_tracks;
  uint32_t i_track;
  uint32_t lut_offset;
  uint8_t* p_lut;
  uint8_t* p_format_metadata;

  intptr_t file_handle = disc_get_file_handle(p_disc);
  int is_double_sided = 0;

  assert(file_handle != k_util_file_no_handle);

  (void) memset(buf, '\0', sizeof(buf));

  p_format_metadata = disc_allocate_format_metadata(p_disc, 512);

  file_len = util_file_handle_read(file_handle, buf, k_max_hfe_size);

  if (file_len == k_max_hfe_size) {
    errx(1, "hfe file too large");
  }

  if (file_len < 512) {
    errx(1, "hfe file no header");
  }
  if (memcmp(buf, "HXCPICFE", 8) != 0) {
    errx(1, "hfe file incorrect header");
  }
  if (buf[8] != '\0') {
    errx(1, "hfe file revision not 0");
  }
  if (buf[11] != 2) {
    errx(1, "hfe encoding not ISOIBM_FM_ENCODING");
  }
  if (buf[10] == 1) {
    is_double_sided = 0;
  } else if (buf[10] == 2) {
    is_double_sided = 1;
  } else {
    errx(1, "hfe invalid number of sides");
  }
  disc_set_is_double_sided(p_disc, is_double_sided);

  hfe_tracks = buf[9];
  if (hfe_tracks > k_ibm_disc_tracks_per_disc) {
    errx(1, "hfe excessive tracks");
  }

  lut_offset = (buf[18] + (buf[19] << 8));
  lut_offset *= 512;

  if ((lut_offset + 512) > file_len) {
    errx(1, "hfe LUT doesn't fit");
  }

  (void) memcpy(p_format_metadata, (buf + lut_offset), 512);
  p_lut = p_format_metadata;

  for (i_track = 0; i_track < hfe_tracks; ++i_track) {
    uint32_t track_offset;
    uint32_t hfe_track_len;
    uint32_t read_track_bytes;
    uint8_t* p_track_lut;
    uint8_t* p_track_data;
    uint32_t i_byte;

    p_track_lut = (p_lut + (i_track * 4));
    track_offset = (p_track_lut[0] + (p_track_lut[1] << 8));
    track_offset *= 512;
    hfe_track_len = (p_track_lut[2] + (p_track_lut[3] << 8));

    if ((track_offset + hfe_track_len) > file_len) {
      errx(1, "hfe track doesn't fit");
    }

    /* Divide by 4; 2x sides and 2x FM bytes per data byte. */
    hfe_track_len /= 4;

    p_track_data = (buf + track_offset);
    read_track_bytes = k_ibm_disc_bytes_per_track;
    if (read_track_bytes > hfe_track_len) {
      read_track_bytes = hfe_track_len;
    }

    disc_select_track(p_disc, i_track);

    for (i_byte = 0; i_byte < read_track_bytes; ++i_byte) {
      uint8_t data;
      uint8_t clock;
      uint8_t* p_data;
      uint8_t* p_clocks;
      uint32_t index = (i_byte / 64);
      index *= 512;
      index += ((i_byte % 64) * 4);

      disc_hfe_extract_data(&data, &clock, (p_track_data + index));
      disc_select_side(p_disc, 0);
      p_data = disc_get_raw_track_data(p_disc);
      p_clocks = disc_get_raw_track_clocks(p_disc);
      p_data[i_byte] = data;
      p_clocks[i_byte] = clock;
      if (is_double_sided) {
        disc_hfe_extract_data(&data, &clock, (p_track_data + index + 256));
        disc_select_side(p_disc, 1);
        p_data = disc_get_raw_track_data(p_disc);
        p_clocks = disc_get_raw_track_clocks(p_disc);
        p_data[i_byte] = data;
        p_clocks[i_byte] = clock;
      }
    }
  }
}

void
disc_hfe_convert(struct disc_struct* p_disc) {
  uint32_t i_track;
  uint8_t header[512];
  uint8_t* p_metadata;

  uint32_t hfe_track_len = (k_ibm_disc_bytes_per_track * 8);
  uint32_t hfe_offset = 2;
  uint32_t hfe_offset_delta = ((hfe_track_len / 512) + 1);
  intptr_t file_handle = disc_get_file_handle(p_disc);
  int is_double_sided = disc_is_double_sided(p_disc);

  /* Fill with 0xFF; that is what the command line HFE tools do, and also, 0xFF
   * appears to be the byte used for the default / sane boolean option.
   */
  (void) memset(header, '\xFF', sizeof(header));
  (void) strcpy((char*) header, "HXCPICFE");
  /* Revision 0. */
  header[8] = 0;
  /* 80 tracks, sides as appropriate. */
  header[9] = 80;
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

  util_file_handle_seek(file_handle, 0);
  util_file_handle_write(file_handle, header, 512);

  p_metadata = disc_allocate_format_metadata(p_disc, 512);

  for (i_track = 0; i_track < k_ibm_disc_tracks_per_disc; ++i_track) {
    uint32_t index = (i_track * 4);
    p_metadata[index] = (hfe_offset & 0xFF);
    p_metadata[index + 1] = (hfe_offset >> 8);
    p_metadata[index + 2] = (hfe_track_len & 0xFF);
    p_metadata[index + 3] = (hfe_track_len >> 8);

    disc_select_track(p_disc, i_track);
    disc_select_side(p_disc, 0);
    disc_set_track_dirty(p_disc, 1);
    disc_hfe_write_track(p_disc);
    if (is_double_sided) {
      disc_select_side(p_disc, 1);
      disc_set_track_dirty(p_disc, 1);
      disc_hfe_write_track(p_disc);
    }

    hfe_offset += hfe_offset_delta;
  }

  disc_set_track_dirty(p_disc, 0);

  util_file_handle_seek(file_handle, 512);
  util_file_handle_write(file_handle, p_metadata, 512);
}
