#include "video.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_mode7_offset = 0x7c00,
  k_mode45_offset = 0x5800,
  k_mode012_offset = 0x3000,
};

enum {
  k_ula_teletext = 0x02,
  k_ula_chars_per_line = 0x0c,
  k_ula_chars_per_line_shift = 2,
  k_ula_clock_speed = 0x10,
  k_ula_clock_speed_shift = 4,
};

struct video_struct {
  unsigned char* p_mem;
  unsigned char video_ula_control;
  unsigned int palette[16];
};

struct video_struct*
video_create(unsigned char* p_mem) {
  struct video_struct* p_video = malloc(sizeof(struct video_struct));
  if (p_video == NULL) {
    errx(1, "cannot allocate video_struct");
  }
  memset(p_video, '\0', sizeof(struct video_struct));

  p_video->p_mem = p_mem;

  return p_video;
}

void
video_destroy(struct video_struct* p_video) {
  free(p_video);
}

static void
video_mode0_render(struct video_struct* p_video, unsigned char* p_frame_buf) {
  unsigned char* p_video_mem = video_get_memory(p_video);
  size_t y;
  for (y = 0; y < 32; ++y) {
    size_t x;
    for (x = 0; x < 80; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        unsigned char packed_pixels = *p_video_mem++;
        unsigned int* p_x_mem = (unsigned int*) p_frame_buf;
        unsigned char p1 = !!(packed_pixels & 0x80);
        unsigned char p2 = !!(packed_pixels & 0x40);
        unsigned char p3 = !!(packed_pixels & 0x20);
        unsigned char p4 = !!(packed_pixels & 0x10);
        unsigned char p5 = !!(packed_pixels & 0x08);
        unsigned char p6 = !!(packed_pixels & 0x04);
        unsigned char p7 = !!(packed_pixels & 0x02);
        unsigned char p8 = !!(packed_pixels & 0x01);
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
video_mode4_render(struct video_struct* p_video, unsigned char* p_frame_buf) {
  unsigned char* p_video_mem = video_get_memory(p_video);
  size_t y;
  for (y = 0; y < 32; ++y) {
    size_t x;
    for (x = 0; x < 40; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        unsigned char packed_pixels = *p_video_mem++;
        unsigned int* p_x_mem = (unsigned int*) p_frame_buf;
        unsigned char p1 = !!(packed_pixels & 0x80);
        unsigned char p2 = !!(packed_pixels & 0x40);
        unsigned char p3 = !!(packed_pixels & 0x20);
        unsigned char p4 = !!(packed_pixels & 0x10);
        unsigned char p5 = !!(packed_pixels & 0x08);
        unsigned char p6 = !!(packed_pixels & 0x04);
        unsigned char p7 = !!(packed_pixels & 0x02);
        unsigned char p8 = !!(packed_pixels & 0x01);
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 16;
        p_x_mem[0] = ~(p1 - 1);
        p_x_mem[1] = ~(p1 - 1);
        p_x_mem[2] = ~(p2 - 1);
        p_x_mem[3] = ~(p2 - 1);
        p_x_mem[4] = ~(p3 - 1);
        p_x_mem[5] = ~(p3 - 1);
        p_x_mem[6] = ~(p4 - 1);
        p_x_mem[7] = ~(p4 - 1);
        p_x_mem[8] = ~(p5 - 1);
        p_x_mem[9] = ~(p5 - 1);
        p_x_mem[10] = ~(p6 - 1);
        p_x_mem[11] = ~(p6 - 1);
        p_x_mem[12] = ~(p7 - 1);
        p_x_mem[13] = ~(p7 - 1);
        p_x_mem[14] = ~(p8 - 1);
        p_x_mem[15] = ~(p8 - 1);
        p_x_mem[640] = ~(p1 - 1);
        p_x_mem[641] = ~(p1 - 1);
        p_x_mem[642] = ~(p2 - 1);
        p_x_mem[643] = ~(p2 - 1);
        p_x_mem[644] = ~(p3 - 1);
        p_x_mem[645] = ~(p3 - 1);
        p_x_mem[646] = ~(p4 - 1);
        p_x_mem[647] = ~(p4 - 1);
        p_x_mem[648] = ~(p5 - 1);
        p_x_mem[649] = ~(p5 - 1);
        p_x_mem[650] = ~(p6 - 1);
        p_x_mem[651] = ~(p6 - 1);
        p_x_mem[652] = ~(p7 - 1);
        p_x_mem[653] = ~(p7 - 1);
        p_x_mem[654] = ~(p8 - 1);
        p_x_mem[655] = ~(p8 - 1);
      }
    }
  }
}

static void
video_mode1_render(struct video_struct* p_video, unsigned char* p_frame_buf) {
  unsigned char* p_video_mem = video_get_memory(p_video);
  unsigned int* p_palette = &p_video->palette[0];
  size_t y;
  for (y = 0; y < 32; ++y) {
    size_t x;
    for (x = 0; x < 80; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        unsigned char packed_pixels = *p_video_mem++;
        /* TODO: lookup table to make this fast. */
        unsigned char v1 = ((packed_pixels & 0x80) >> 6) |
                           ((packed_pixels & 0x08) >> 3);
        unsigned char v2 = ((packed_pixels & 0x40) >> 5) |
                           ((packed_pixels & 0x04) >> 2);
        unsigned char v3 = ((packed_pixels & 0x20) >> 4) |
                           ((packed_pixels & 0x02) >> 1);
        unsigned char v4 = ((packed_pixels & 0x10) >> 3) |
                           ((packed_pixels & 0x01) >> 0);
        unsigned int p1 = p_palette[4 + (v1 << 1)];
        unsigned int p2 = p_palette[4 + (v2 << 1)];
        unsigned int p3 = p_palette[4 + (v3 << 1)];
        unsigned int p4 = p_palette[4 + (v4 << 1)];
        unsigned int* p_x_mem = (unsigned int*) p_frame_buf;
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 8;
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
video_mode5_render(struct video_struct* p_video, unsigned char* p_frame_buf) {
  unsigned char* p_video_mem = video_get_memory(p_video);
  unsigned int* p_palette = &p_video->palette[0];
  size_t y;
  for (y = 0; y < 32; ++y) {
    size_t x;
    for (x = 0; x < 40; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        unsigned char packed_pixels = *p_video_mem++;
        /* TODO: lookup table to make this fast. */
        unsigned char v1 = ((packed_pixels & 0x80) >> 6) |
                           ((packed_pixels & 0x08) >> 3);
        unsigned char v2 = ((packed_pixels & 0x40) >> 5) |
                           ((packed_pixels & 0x04) >> 2);
        unsigned char v3 = ((packed_pixels & 0x20) >> 4) |
                           ((packed_pixels & 0x02) >> 1);
        unsigned char v4 = ((packed_pixels & 0x10) >> 3) |
                           ((packed_pixels & 0x01) >> 0);
        unsigned int p1 = p_palette[(1 << v1) - 1];
        unsigned int p2 = p_palette[(1 << v2) - 1];
        unsigned int p3 = p_palette[(1 << v3) - 1];
        unsigned int p4 = p_palette[(1 << v4) - 1];
        unsigned int* p_x_mem = (unsigned int*) p_frame_buf;
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 16;
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
video_mode2_render(struct video_struct* p_video, unsigned char* p_frame_buf) {
  unsigned char* p_video_mem = video_get_memory(p_video);
  unsigned int* p_palette = &p_video->palette[0];
  size_t y;
  for (y = 0; y < 32; ++y) {
    size_t x;
    for (x = 0; x < 80; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        unsigned char packed_pixels = *p_video_mem++;
        /* TODO: lookup table to make this fast. */
        unsigned char v1 = ((packed_pixels & 0x80) >> 4) |
                           ((packed_pixels & 0x20) >> 3) |
                           ((packed_pixels & 0x08) >> 2) |
                           ((packed_pixels & 0x02) >> 1);
        unsigned char v2 = ((packed_pixels & 0x40) >> 3) |
                           ((packed_pixels & 0x10) >> 2) |
                           ((packed_pixels & 0x04) >> 1) |
                           ((packed_pixels & 0x01) >> 0);
        unsigned int p1 = p_palette[v1];
        unsigned int p2 = p_palette[v2];
        unsigned int* p_x_mem = (unsigned int*) p_frame_buf;
        p_x_mem += ((y * 8) + y2) * 2 * 640;
        p_x_mem += x * 8;
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
  unsigned char ula_control = video_get_ula_control(p_video);
  unsigned char ula_chars_per_line =
      (ula_control & k_ula_chars_per_line) >> k_ula_chars_per_line_shift;
  return 1 << (3 - ula_chars_per_line);
}

static size_t
video_get_clock_speed(struct video_struct* p_video) {
  unsigned char ula_control = video_get_ula_control(p_video);
  unsigned char clock_speed =
      (ula_control & k_ula_clock_speed) >> k_ula_clock_speed_shift;
  return clock_speed;
}

void
video_render(struct video_struct* p_video,
             unsigned char* p_x_mem,
             size_t x,
             size_t y,
             size_t bpp) {
  size_t pixel_width;
  size_t clock_speed;

  assert(x == 640);
  assert(y == 512);
  assert(bpp == 4);

  assert(!video_is_text(p_video));

  pixel_width = video_get_pixel_width(p_video);
  clock_speed = video_get_clock_speed(p_video);

  if (pixel_width == 1)  {
    assert(clock_speed == 1);
    video_mode0_render(p_video, p_x_mem);
  } else if (pixel_width == 2 && clock_speed == 0) {
    video_mode4_render(p_video, p_x_mem);
  } else if (pixel_width == 2 && clock_speed == 1) {
    video_mode1_render(p_video, p_x_mem);
  } else if (pixel_width == 4 && clock_speed == 1) {
    video_mode2_render(p_video, p_x_mem);
  } else if (pixel_width == 4 && clock_speed == 0) {
    video_mode5_render(p_video, p_x_mem);
  } else if (pixel_width == 8) {
    /* Ignore for now: could be custom mode, or more likely control register
     * just not set.
     */
  } else {
    assert(0);
  }
}

unsigned char
video_get_ula_control(struct video_struct* p_video) {
  return p_video->video_ula_control;
}

void
video_set_ula_control(struct video_struct* p_video, unsigned char val) {
  p_video->video_ula_control = val;
}

void
video_set_ula_palette(struct video_struct* p_video, unsigned char val) {
  unsigned char index = (val >> 4);
  unsigned char rgbf = (val & 0x0f);
  unsigned int color = 0xff000000;

  if (!(rgbf & 0x1)) {
    color |= 0x00ff0000;
  }
  if (!(rgbf & 0x2)) {
    color |= 0x0000ff00;
  }
  if (!(rgbf & 0x4)) {
    color |= 0x000000ff;
  }

  p_video->palette[index] = color;
}

unsigned char*
video_get_memory(struct video_struct* p_video) {
  unsigned char ula_control = video_get_ula_control(p_video);
  size_t offset;
  if (ula_control & k_ula_teletext) {
    offset = k_mode7_offset;
  } else if (video_get_clock_speed(p_video) == 1) {
    offset = k_mode012_offset;
  } else {
    offset = k_mode45_offset;
  }
  return p_video->p_mem + offset;
}

int
video_is_text(struct video_struct* p_video) {
  unsigned char ula_control = video_get_ula_control(p_video);
  if (ula_control & k_ula_teletext) {
    return 1;
  }
  return 0;
}
