#include "video.h"

#include "util.h"
#include "timing.h"
#include "via.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A real BBC in its default modes actually has a CRTC period of 19968us,
 * just short of 20ms.
 * The calculation, with horizontal total 128, vertical total 39, lines
 * per character 8, 2Mhz clock, is:
 * (128 * 39 * 8) / 2
 */
static const size_t k_video_us_per_vsync = 19968; /* about 20ms / 50Hz */

enum {
  k_ula_addr_control = 0,
  k_ula_addr_palette = 1,
};

enum {
  k_ula_teletext = 0x02,
  k_ula_chars_per_line = 0x0c,
  k_ula_chars_per_line_shift = 2,
  k_ula_clock_speed = 0x10,
  k_ula_clock_speed_shift = 4,
};

enum {
  k_crtc_addr_reg = 0,
  k_crtc_addr_val = 1,
};

enum {
  k_crtc_reg_horiz_total = 0,
  k_crtc_reg_horiz_displayed = 1,
  k_crtc_reg_horiz_position = 2,
  k_crtc_reg_sync_width = 3,
  k_crtc_reg_vert_total = 4,
  k_crtc_reg_vert_adjust = 5,
  k_crtc_reg_vert_displayed = 6,
  k_crtc_reg_vert_position = 7,
  k_crtc_reg_lines_per_character = 9,
  k_crtc_reg_mem_addr_high = 12,
  k_crtc_reg_mem_addr_low = 13,
};

struct video_struct {
  uint8_t* p_mem;
  int externally_clocked;
  struct timing_struct* p_timing;
  struct via_struct* p_system_via;
  size_t video_timer_id;
  uint8_t* p_sysvia_IC32;
  void (*p_framebuffer_ready_callback)();
  void* p_framebuffer_ready_object;

  uint64_t wall_time;
  uint64_t vsync_next_time;

  uint8_t video_ula_control;
  uint8_t video_palette[16];
  uint32_t palette[16];

  uint8_t teletext_line[k_bbc_mode7_width];

  uint8_t crtc_address;
  /* R1 */
  uint8_t crtc_horiz_displayed;
  /* R2 */
  uint8_t crtc_horiz_position;
  /* R5 */
  uint8_t crtc_vert_adjust;
  /* R6 */
  uint8_t crtc_vert_displayed;
  /* R7 */
  uint8_t crtc_vert_position;
  /* R12 */
  uint8_t crtc_mem_addr_high;
  /* R13 */
  uint8_t crtc_mem_addr_low;

  size_t prev_horiz_chars;
  size_t prev_vert_chars;
  int prev_horiz_chars_offset;
  int prev_vert_lines_offset;
};

static void
video_init_timer(struct video_struct* p_video) {
  if (p_video->externally_clocked) {
    return;
  }
  (void) timing_start_timer_with_value(p_video->p_timing,
                                       p_video->video_timer_id,
                                       0);
}

static void
video_update_timer(struct video_struct* p_video) {
  int64_t timer_value;
  uint32_t line_clocks;
  uint32_t character_line_clocks;
  uint32_t frame_clocks;
  uint32_t vsync_pulse_clocks;

  struct timing_struct* p_timing = p_video->p_timing;
  size_t timer_id = p_video->video_timer_id;

  if (p_video->externally_clocked) {
    return;
  }

  timer_value = timing_get_timer_value(p_timing, timer_id);
  assert(timer_value == 0);

  line_clocks = (127 + 1);
  character_line_clocks = (line_clocks * (7 + 1));
  frame_clocks = (character_line_clocks * (38 + 1));
  frame_clocks += (character_line_clocks * p_video->crtc_vert_adjust);
  vsync_pulse_clocks = (character_line_clocks * 2);

  (void) timing_set_timer_value(p_timing, timer_id, frame_clocks);

  (void) timer_value;
  (void) vsync_pulse_clocks;
}

static void
video_timer_fired(void* p) {
  struct video_struct* p_video = (struct video_struct*) p;

  assert(!p_video->externally_clocked);

  via_raise_interrupt(p_video->p_system_via, k_int_CA1);

  p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object);

  video_update_timer(p_video);
}

struct video_struct*
video_create(uint8_t* p_mem,
             int externally_clocked,
             struct timing_struct* p_timing,
             struct via_struct* p_system_via,
             void (*p_framebuffer_ready_callback)(void* p),
             void* p_framebuffer_ready_object) {
  struct video_struct* p_video = malloc(sizeof(struct video_struct));
  if (p_video == NULL) {
    errx(1, "cannot allocate video_struct");
  }

  /* This effectively zero initializes all of the CRTC and ULA registers.
   * The emulators are not consistent here, but zero initialization is a
   * popular choice.
   * The 6845 data sheet isn't much help, quoting:
   * http://bitsavers.trailing-edge.com/components/motorola/_dataSheets/6845.pdf
   * "The CRTC registers will have an initial value at power up. When using
   * a direct drive monitor (sans horizontal oscillator) these initial values
   * may result in out-of-tolerance operation."
   * It isn't specified whether those initial values are random or
   * deterministic, or what they may be. At any rate, the MOS ROM sets values
   * for the registers as part of selecting MODE7 at boot up.
   */
  (void) memset(p_video, '\0', sizeof(struct video_struct));

  p_video->p_mem = p_mem;
  p_video->externally_clocked = externally_clocked;
  p_video->p_timing = p_timing;
  p_video->p_system_via = p_system_via;
  p_video->p_sysvia_IC32 = via_get_peripheral_b_ptr(p_system_via);
  p_video->p_framebuffer_ready_callback = p_framebuffer_ready_callback;
  p_video->p_framebuffer_ready_object = p_framebuffer_ready_object;

  p_video->wall_time = 0;
  p_video->vsync_next_time = 0;

  p_video->video_timer_id = timing_register_timer(p_timing,
                                                  video_timer_fired,
                                                  p_video);
  video_init_timer(p_video);
  video_update_timer(p_video);

  return p_video;
}

void
video_destroy(struct video_struct* p_video) {
  free(p_video);
}

void
video_apply_wall_time_delta(struct video_struct* p_video, uint64_t delta) {
  uint64_t wall_time;

  if (!p_video->externally_clocked) {
    return;
  }

  wall_time = (p_video->wall_time + delta);
  p_video->wall_time = wall_time;

  if (wall_time < p_video->vsync_next_time) {
    return;
  }
  while (p_video->vsync_next_time <= wall_time) {
    p_video->vsync_next_time += k_video_us_per_vsync;
  }

  via_raise_interrupt(p_video->p_system_via, k_int_CA1);

  p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object);
}

static void
video_mode0_render(struct video_struct* p_video,
                   uint8_t* p_frame_buf,
                   size_t horiz_chars,
                   size_t vert_chars) {
  size_t y;
  uint8_t* p_video_mem = video_get_memory(p_video, 0, 0);
  size_t video_memory_size = video_get_memory_size(p_video);

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      if (((size_t) p_video_mem & 0xffff) == 0x8000) {
        p_video_mem -= video_memory_size;
      }
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t packed_pixels = *p_video_mem++;
        uint32_t* p_x_mem = (uint32_t*) p_frame_buf;
        uint8_t p1 = !!(packed_pixels & 0x80);
        uint8_t p2 = !!(packed_pixels & 0x40);
        uint8_t p3 = !!(packed_pixels & 0x20);
        uint8_t p4 = !!(packed_pixels & 0x10);
        uint8_t p5 = !!(packed_pixels & 0x08);
        uint8_t p6 = !!(packed_pixels & 0x04);
        uint8_t p7 = !!(packed_pixels & 0x02);
        uint8_t p8 = !!(packed_pixels & 0x01);
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 8;
        p_x_mem[0] = ~(p1 - 1);
        p_x_mem[1] = ~(p2 - 1);
        p_x_mem[2] = ~(p3 - 1);
        p_x_mem[3] = ~(p4 - 1);
        p_x_mem[4] = ~(p5 - 1);
        p_x_mem[5] = ~(p6 - 1);
        p_x_mem[6] = ~(p7 - 1);
        p_x_mem[7] = ~(p8 - 1);
        p_x_mem[640] = ~(p1 - 1);
        p_x_mem[641] = ~(p2 - 1);
        p_x_mem[642] = ~(p3 - 1);
        p_x_mem[643] = ~(p4 - 1);
        p_x_mem[644] = ~(p5 - 1);
        p_x_mem[645] = ~(p6 - 1);
        p_x_mem[646] = ~(p7 - 1);
        p_x_mem[647] = ~(p8 - 1);
      }
    }
  }
}

static void
video_mode4_render(struct video_struct* p_video,
                   uint8_t* p_frame_buf,
                   size_t horiz_chars,
                   size_t vert_chars) {
  size_t y;
  uint8_t* p_video_mem = video_get_memory(p_video, 0, 0);
  size_t video_memory_size = video_get_memory_size(p_video);
  uint32_t* p_palette = &p_video->palette[0];

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      if (((size_t) p_video_mem & 0xffff) == 0x8000) {
        p_video_mem -= video_memory_size;
      }
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t packed_pixels = *p_video_mem++;
        uint32_t* p_x_mem = (uint32_t*) p_frame_buf;
        uint32_t p1 = p_palette[((packed_pixels & 0x80) >> 4)];
        uint32_t p2 = p_palette[((packed_pixels & 0x40) >> 3)];
        uint32_t p3 = p_palette[((packed_pixels & 0x20) >> 2)];
        uint32_t p4 = p_palette[((packed_pixels & 0x10) >> 1)];
        uint32_t p5 = p_palette[(packed_pixels & 0x08)];
        uint32_t p6 = p_palette[((packed_pixels & 0x04) << 1)];
        uint32_t p7 = p_palette[((packed_pixels & 0x02) << 2)];
        uint32_t p8 = p_palette[((packed_pixels & 0x01) << 3)];
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 16;
        p_x_mem[0] = p1;
        p_x_mem[1] = p1;
        p_x_mem[2] = p2;
        p_x_mem[3] = p2;
        p_x_mem[4] = p3;
        p_x_mem[5] = p3;
        p_x_mem[6] = p4;
        p_x_mem[7] = p4;
        p_x_mem[8] = p5;
        p_x_mem[9] = p5;
        p_x_mem[10] = p6;
        p_x_mem[11] = p6;
        p_x_mem[12] = p7;
        p_x_mem[13] = p7;
        p_x_mem[14] = p8;
        p_x_mem[15] = p8;
        p_x_mem[640] = p1;
        p_x_mem[641] = p1;
        p_x_mem[642] = p2;
        p_x_mem[643] = p2;
        p_x_mem[644] = p3;
        p_x_mem[645] = p3;
        p_x_mem[646] = p4;
        p_x_mem[647] = p4;
        p_x_mem[648] = p5;
        p_x_mem[649] = p5;
        p_x_mem[650] = p6;
        p_x_mem[651] = p6;
        p_x_mem[652] = p7;
        p_x_mem[653] = p7;
        p_x_mem[654] = p8;
        p_x_mem[655] = p8;
      }
    }
  }
}

static void
video_mode1_render(struct video_struct* p_video,
                   uint8_t* p_frame_buf,
                   size_t horiz_chars,
                   size_t vert_chars,
                   size_t horiz_chars_offset,
                   size_t vert_lines_offset) {
  size_t y;
  uint8_t* p_video_mem = video_get_memory(p_video, 0, 0);
  size_t video_memory_size = video_get_memory_size(p_video);
  uint32_t* p_palette = &p_video->palette[0];

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      if (((size_t) p_video_mem & 0xffff) == 0x8000) {
        p_video_mem -= video_memory_size;
      }
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t packed_pixels = *p_video_mem++;
        /* TODO: lookup table to make this fast. */
        uint8_t v1 = ((packed_pixels & 0x80) >> 6) |
                           ((packed_pixels & 0x08) >> 3);
        uint8_t v2 = ((packed_pixels & 0x40) >> 5) |
                           ((packed_pixels & 0x04) >> 2);
        uint8_t v3 = ((packed_pixels & 0x20) >> 4) |
                           ((packed_pixels & 0x02) >> 1);
        uint8_t v4 = ((packed_pixels & 0x10) >> 3) |
                           ((packed_pixels & 0x01) >> 0);
        uint32_t p1 = p_palette[4 + (v1 << 1)];
        uint32_t p2 = p_palette[4 + (v2 << 1)];
        uint32_t p3 = p_palette[4 + (v3 << 1)];
        uint32_t p4 = p_palette[4 + (v4 << 1)];
        uint32_t* p_x_mem = (uint32_t*) p_frame_buf;
        p_x_mem += (((y * 8) + y2 + vert_lines_offset) * 2 * 640);
        p_x_mem += ((x + horiz_chars_offset) * 8);
        p_x_mem[0] = p1;
        p_x_mem[1] = p1;
        p_x_mem[2] = p2;
        p_x_mem[3] = p2;
        p_x_mem[4] = p3;
        p_x_mem[5] = p3;
        p_x_mem[6] = p4;
        p_x_mem[7] = p4;
        p_x_mem[640] = p1;
        p_x_mem[641] = p1;
        p_x_mem[642] = p2;
        p_x_mem[643] = p2;
        p_x_mem[644] = p3;
        p_x_mem[645] = p3;
        p_x_mem[646] = p4;
        p_x_mem[647] = p4;
      }
    }
  }
}

static void
video_mode5_render(struct video_struct* p_video,
                   uint8_t* p_frame_buf,
                   size_t horiz_chars,
                   size_t vert_chars,
                   size_t horiz_chars_offset,
                   size_t vert_lines_offset) {
  size_t y;
  uint8_t* p_video_mem = video_get_memory(p_video, 0, 0);
  size_t video_memory_size = video_get_memory_size(p_video);
  uint32_t* p_palette = &p_video->palette[0];

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      if (((size_t) p_video_mem & 0xffff) == 0x8000) {
        p_video_mem -= video_memory_size;
      }
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t packed_pixels = *p_video_mem++;
        /* TODO: lookup table to make this fast. */
        uint8_t v1 = ((packed_pixels & 0x80) >> 6) |
                           ((packed_pixels & 0x08) >> 3);
        uint8_t v2 = ((packed_pixels & 0x40) >> 5) |
                           ((packed_pixels & 0x04) >> 2);
        uint8_t v3 = ((packed_pixels & 0x20) >> 4) |
                           ((packed_pixels & 0x02) >> 1);
        uint8_t v4 = ((packed_pixels & 0x10) >> 3) |
                           ((packed_pixels & 0x01) >> 0);
        uint32_t p1 = p_palette[4 + (v1 << 1)];
        uint32_t p2 = p_palette[4 + (v2 << 1)];
        uint32_t p3 = p_palette[4 + (v3 << 1)];
        uint32_t p4 = p_palette[4 + (v4 << 1)];
        uint32_t* p_x_mem = (uint32_t*) p_frame_buf;
        p_x_mem += (((y * 8) + y2 + vert_lines_offset) * 2 * 640);
        p_x_mem += ((x + horiz_chars_offset) * 16);
        p_x_mem[0] = p1;
        p_x_mem[1] = p1;
        p_x_mem[2] = p1;
        p_x_mem[3] = p1;
        p_x_mem[4] = p2;
        p_x_mem[5] = p2;
        p_x_mem[6] = p2;
        p_x_mem[7] = p2;
        p_x_mem[8] = p3;
        p_x_mem[9] = p3;
        p_x_mem[10] = p3;
        p_x_mem[11] = p3;
        p_x_mem[12] = p4;
        p_x_mem[13] = p4;
        p_x_mem[14] = p4;
        p_x_mem[15] = p4;
        p_x_mem[640] = p1;
        p_x_mem[641] = p1;
        p_x_mem[642] = p1;
        p_x_mem[643] = p1;
        p_x_mem[644] = p2;
        p_x_mem[645] = p2;
        p_x_mem[646] = p2;
        p_x_mem[647] = p2;
        p_x_mem[648] = p3;
        p_x_mem[649] = p3;
        p_x_mem[650] = p3;
        p_x_mem[651] = p3;
        p_x_mem[652] = p4;
        p_x_mem[653] = p4;
        p_x_mem[654] = p4;
        p_x_mem[655] = p4;
      }
    }
  }
}

static void
video_mode2_render(struct video_struct* p_video,
                   uint8_t* p_frame_buf,
                   size_t horiz_chars,
                   size_t vert_chars,
                   size_t horiz_chars_offset,
                   size_t vert_lines_offset) {
  size_t i;
  size_t y;
  uint8_t* p_video_mem = video_get_memory(p_video, 0, 0);
  size_t video_memory_size = video_get_memory_size(p_video);
  uint32_t* p_palette = &p_video->palette[0];

  uint32_t p1s[256];
  uint32_t p2s[256];
  for (i = 0; i < 256; ++i) {
    uint8_t v1 = ((i & 0x80) >> 4) |
                       ((i & 0x20) >> 3) |
                       ((i & 0x08) >> 2) |
                       ((i & 0x02) >> 1);
    uint8_t v2 = ((i & 0x40) >> 3) |
                       ((i & 0x10) >> 2) |
                       ((i & 0x04) >> 1) |
                       ((i & 0x01) >> 0);
    uint32_t p1 = p_palette[v1];
    uint32_t p2 = p_palette[v2];

    p1s[i] = p1;
    p2s[i] = p2;
  }

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      if (((size_t) p_video_mem & 0xffff) == 0x8000) {
        p_video_mem -= video_memory_size;
      }
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t packed_pixels = *p_video_mem++;
        uint32_t p1 = p1s[packed_pixels];
        uint32_t p2 = p2s[packed_pixels];
        uint32_t* p_x_mem = (uint32_t*) p_frame_buf;
        p_x_mem += (((y * 8) + y2 + vert_lines_offset) * 2 * 640);
        p_x_mem += ((x + horiz_chars_offset) * 8);
        p_x_mem[0] = p1;
        p_x_mem[1] = p1;
        p_x_mem[2] = p1;
        p_x_mem[3] = p1;
        p_x_mem[4] = p2;
        p_x_mem[5] = p2;
        p_x_mem[6] = p2;
        p_x_mem[7] = p2;
        p_x_mem[640] = p1;
        p_x_mem[641] = p1;
        p_x_mem[642] = p1;
        p_x_mem[643] = p1;
        p_x_mem[644] = p2;
        p_x_mem[645] = p2;
        p_x_mem[646] = p2;
        p_x_mem[647] = p2;
      }
    }
  }
}

static size_t
video_get_pixel_width(struct video_struct* p_video) {
  uint8_t ula_control = video_get_ula_control(p_video);
  uint8_t ula_chars_per_line =
      (ula_control & k_ula_chars_per_line) >> k_ula_chars_per_line_shift;
  return 1 << (3 - ula_chars_per_line);
}

static size_t
video_get_clock_speed(struct video_struct* p_video) {
  uint8_t ula_control = video_get_ula_control(p_video);
  uint8_t clock_speed =
      (ula_control & k_ula_clock_speed) >> k_ula_clock_speed_shift;
  return clock_speed;
}

void
video_render(struct video_struct* p_video,
             uint8_t* p_x_mem,
             size_t x,
             size_t y,
             size_t bpp) {
  size_t pixel_width;
  size_t clock_speed;
  size_t horiz_chars;
  size_t vert_chars;
  size_t vert_lines;
  int horiz_chars_offset;
  int vert_lines_offset;

  size_t max_horiz_chars;
  size_t max_vert_lines;

  assert(x == 640);
  assert(y == 512);
  assert(bpp == 4);

  pixel_width = video_get_pixel_width(p_video);
  clock_speed = video_get_clock_speed(p_video);

  max_vert_lines = 256;
  if (clock_speed == 0) {
    max_horiz_chars = 40;
  } else {
    max_horiz_chars = 80;
  }

  horiz_chars = video_get_horiz_chars(p_video, clock_speed);
  vert_chars = video_get_vert_chars(p_video);
  vert_lines = (vert_chars * 8);
  horiz_chars_offset = video_get_horiz_chars_offset(p_video, clock_speed);
  vert_lines_offset = video_get_vert_lines_offset(p_video);
  assert(horiz_chars_offset >= 0);
  assert(vert_lines_offset >= 0);

  if (horiz_chars > max_horiz_chars) {
    horiz_chars = max_horiz_chars;
  }
  if (vert_lines > max_vert_lines) {
    vert_lines = max_vert_lines;
  }
  if ((size_t) horiz_chars_offset > max_horiz_chars) {
    horiz_chars_offset = max_horiz_chars;
  }
  if ((size_t) vert_lines_offset > max_vert_lines) {
    vert_lines_offset = max_vert_lines;
  }
  if (horiz_chars + horiz_chars_offset > max_horiz_chars) {
    horiz_chars = (max_horiz_chars - horiz_chars_offset);
  }
  if (vert_lines + vert_lines_offset > max_vert_lines) {
    vert_lines = (max_vert_lines - vert_lines_offset);
    vert_chars = (vert_lines / 8);
  }

  /* Clear the screen if the size or position changed. */
  if (horiz_chars != p_video->prev_horiz_chars ||
      vert_chars != p_video->prev_vert_chars ||
      horiz_chars_offset != p_video->prev_horiz_chars_offset ||
      vert_lines_offset != p_video->prev_vert_lines_offset) {
    (void) memset(p_x_mem, '\0', x * y * bpp);
  }
  p_video->prev_horiz_chars = horiz_chars;
  p_video->prev_vert_chars = vert_chars;
  p_video->prev_horiz_chars_offset = horiz_chars_offset;
  p_video->prev_vert_lines_offset = vert_lines_offset;

  if (pixel_width == 1)  {
    video_mode0_render(p_video, p_x_mem, horiz_chars, vert_chars);
  } else if (pixel_width == 2 && clock_speed == 0) {
    video_mode4_render(p_video, p_x_mem, horiz_chars, vert_chars);
  } else if (pixel_width == 2 && clock_speed == 1) {
    video_mode1_render(p_video,
                       p_x_mem,
                       horiz_chars,
                       vert_chars,
                       horiz_chars_offset,
                       vert_lines_offset);
  } else if (pixel_width == 4 && clock_speed == 1) {
    video_mode2_render(p_video,
                       p_x_mem,
                       horiz_chars,
                       vert_chars,
                       horiz_chars_offset,
                       vert_lines_offset);
  } else if (pixel_width == 4 && clock_speed == 0) {
    video_mode5_render(p_video,
                       p_x_mem,
                       horiz_chars,
                       vert_chars,
                       horiz_chars_offset,
                       vert_lines_offset);
  } else {
    /* Ignore for now: could be custom mode, or more likely control register
     * just not set.
     * Can also be a race conditions vs. the main 6502 thread.
     */
  }
}

void
video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t index;
  uint8_t rgbf;
  uint32_t color;

  if (addr == k_ula_addr_control) {
    p_video->video_ula_control = val;
    return;
  }

  assert(addr == k_ula_addr_palette);

  index = (val >> 4);
  rgbf = (val & 0x0f);
  /* Alpha. */
  color = 0xff000000;
  p_video->video_palette[index] = rgbf;
  /* Red. */
  if (!(rgbf & 0x1)) {
    color |= 0x00ff0000;
  }
  /* Green. */
  if (!(rgbf & 0x2)) {
    color |= 0x0000ff00;
  }
  /* Blue. */
  if (!(rgbf & 0x4)) {
    color |= 0x000000ff;
  }
  p_video->palette[index] = color;
}

void
video_crtc_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t hsync_width;
  uint8_t vsync_width;
  uint8_t reg;

  if (addr == k_crtc_addr_reg) {
    p_video->crtc_address = val;
    return;
  }

  assert(addr == k_crtc_addr_val);

  reg = p_video->crtc_address;

  switch (reg) {
  case k_crtc_reg_horiz_total:
    if ((val != 63) && (val != 127)) {
      printf("LOG:CRTC:unusual horizontal total: %d\n", val);
    }
    break;
  case k_crtc_reg_vert_total:
    if ((val != 38) && (val != 30)) {
      printf("LOG:CRTC:unusual vertical total: %d\n", val);
    }
    break;
  case k_crtc_reg_sync_width:
    hsync_width = (val & 0xF);
    if ((hsync_width != 8) && (hsync_width != 4)) {
      printf("LOG:CRTC:unusual hsync width: %d\n", hsync_width);
    }
    vsync_width = (val >> 4);
    if (vsync_width != 2) {
      printf("LOG:CRTC:unusual vsync width: %d\n", vsync_width);
    }
    break;
  case k_crtc_reg_lines_per_character:
    if (val != 7) {
      printf("LOG:CRTC:scan lines per character != 7: %d\n", val);
    }
    break;
  case k_crtc_reg_mem_addr_high:
    p_video->crtc_mem_addr_high = (val & 0x3f);
    break;
  case k_crtc_reg_mem_addr_low:
    p_video->crtc_mem_addr_low = val;
    break;
  case k_crtc_reg_horiz_displayed:
    p_video->crtc_horiz_displayed = val;
    break;
  case k_crtc_reg_horiz_position:
    p_video->crtc_horiz_position = val;
    break;
  case k_crtc_reg_vert_displayed:
    p_video->crtc_vert_displayed = val;
    break;
  case k_crtc_reg_vert_position:
    p_video->crtc_vert_position = val;
    break;
  case k_crtc_reg_vert_adjust:
    p_video->crtc_vert_adjust = val;
    break;
  default:
    break;
  }
}

uint8_t
video_get_ula_control(struct video_struct* p_video) {
  return p_video->video_ula_control;
}

void
video_set_ula_control(struct video_struct* p_video, uint8_t val) {
  video_ula_write(p_video, k_ula_addr_control, val);
}

void
video_get_ula_full_palette(struct video_struct* p_video,
                           uint8_t* p_values) {
  size_t i;
  for (i = 0; i < 16; ++i) {
    p_values[i] = p_video->video_palette[i];
  }
}

void
video_set_ula_full_palette(struct video_struct* p_video,
                           const uint8_t* p_values) {
  size_t i;
  for (i = 0; i < 16; ++i) {
    uint8_t val = p_values[i] & 0x0f;
    val |= (i << 4);
    video_ula_write(p_video, k_ula_addr_palette, val);
  }
}

void
video_get_crtc_registers(struct video_struct* p_video,
                         uint8_t* p_values) {
  (void) memset(p_values, '\0', 18);
  p_values[k_crtc_reg_horiz_displayed] = p_video->crtc_horiz_displayed;
  p_values[k_crtc_reg_horiz_position] = p_video->crtc_horiz_position;
  p_values[k_crtc_reg_vert_displayed] = p_video->crtc_vert_displayed;
  p_values[k_crtc_reg_vert_position] = p_video->crtc_vert_position;
  p_values[k_crtc_reg_vert_adjust] = p_video->crtc_vert_adjust;
  p_values[k_crtc_reg_mem_addr_high] = p_video->crtc_mem_addr_high;
  p_values[k_crtc_reg_mem_addr_low] = p_video->crtc_mem_addr_low;
}

void
video_set_crtc_registers(struct video_struct* p_video,
                         const uint8_t* p_values) {
  p_video->crtc_horiz_displayed = p_values[k_crtc_reg_horiz_displayed];
  p_video->crtc_horiz_position = p_values[k_crtc_reg_horiz_position];
  p_video->crtc_vert_displayed = p_values[k_crtc_reg_vert_displayed];
  p_video->crtc_vert_position = p_values[k_crtc_reg_vert_position];
  p_video->crtc_vert_adjust = p_values[k_crtc_reg_vert_adjust];
  p_video->crtc_mem_addr_high = p_values[k_crtc_reg_mem_addr_high];
  p_video->crtc_mem_addr_low = p_values[k_crtc_reg_mem_addr_low];
}

uint8_t*
video_get_memory(struct video_struct* p_video, size_t offset, size_t len) {
  size_t mem_offset;

  uint8_t ula_control = video_get_ula_control(p_video);
  int is_text = (ula_control & k_ula_teletext);
  uint8_t* p_mem = p_video->p_mem;

  assert(offset < 0x5000);

  if (is_text) {
    mem_offset = p_video->crtc_mem_addr_high;
    mem_offset ^= 0x20;
    mem_offset += 0x74;
    mem_offset <<= 8;
    mem_offset |= p_video->crtc_mem_addr_low;
  } else {
    mem_offset = ((p_video->crtc_mem_addr_high << 8) |
                  p_video->crtc_mem_addr_low);
    mem_offset <<= 3;
  }

  mem_offset &= 0x7fff;
  mem_offset += offset;
  if (mem_offset >= 0x8000) {
    if (is_text) {
      mem_offset -= 0x400;
    } else {
      size_t memory_size = video_get_memory_size(p_video);
      mem_offset -= memory_size;
    }
  }

  if (is_text && len == k_bbc_mode7_width) {
    assert(mem_offset < 0x8000);
    if (mem_offset + len > 0x8000) {
      uint8_t* p_line = &p_video->teletext_line[0];
      size_t pre_len = 0x8000 - mem_offset;
      size_t post_len = k_bbc_mode7_width - pre_len;
      memcpy(p_line, p_mem + mem_offset, pre_len);
      memcpy(p_line + pre_len, p_mem + 0x7c00, post_len);
      return p_line;
    }
  }

  /* Need alignment; we check for the pointer low bytes crossing 0x8000. */
  assert(((size_t) p_mem & 0xffff) == 0);
  p_mem += mem_offset;
  return p_mem;
}

size_t
video_get_memory_size(struct video_struct* p_video) {
  size_t ret;
  size_t size = *(p_video->p_sysvia_IC32);
  size >>= 4;
  size &= 3;
  /* Note: doesn't seem to match the BBC Microcomputer Advanced User Guide, but
   * does work and does match b-em.
   */
  switch (size) {
  case 0:
    ret = 0x4000;
    break;
  case 1:
    ret = 0x2000;
    break;
  case 2:
    ret = 0x5000;
    break;
  case 3:
    ret = 0x2800;
    break;
  default:
    assert(0);
    break;
  }

  return ret;
}

size_t
video_get_horiz_chars(struct video_struct* p_video, size_t clock_speed) {
  /* NOTE: clock_speed is passed in rather than fetched, to avoid race
   * conditions where the BBC thread is changing values from under us.
   */
  size_t ret = p_video->crtc_horiz_displayed;

  if (clock_speed == 1 && ret > 80) {
    ret = 80;
  } else if (clock_speed == 0 && ret > 40) {
    ret = 40;
  }

  return ret;
}

size_t
video_get_vert_chars(struct video_struct* p_video) {
  size_t ret = p_video->crtc_vert_displayed;
  if (ret > 32) {
    ret = 32;
  }

  return ret;
}

int
video_get_horiz_chars_offset(struct video_struct* p_video, size_t clock_speed) {
  /* NOTE: clock_speed is passed in rather than fetched, to avoid race
   * conditions where the BBC thread is changing values from under us.
   */
  int ret = p_video->crtc_horiz_position;
  int max = 49;
  if (clock_speed == 1) {
    max = 98;
  }
  if (ret > max) {
    ret = max;
  }

  return (max - ret);
}

int
video_get_vert_lines_offset(struct video_struct* p_video) {
  int ret;
  uint8_t pos = p_video->crtc_vert_position;
  uint8_t adjust = (p_video->crtc_vert_adjust & 0x1f);
  if (pos > 34) {
    pos = 34;
  }

  ret = ((34 - pos) * 8);
  ret += adjust;
  return ret;
}

int
video_is_text(struct video_struct* p_video) {
  uint8_t ula_control = video_get_ula_control(p_video);
  if (ula_control & k_ula_teletext) {
    return 1;
  }
  return 0;
}
