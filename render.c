#include "render.h"

#include "bbc_options.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct render_struct {
  uint32_t width;
  uint32_t height;

  uint32_t* p_buffer;
  uint32_t* p_buffer_end;

  uint32_t palette[16];
  int render_table_dirty[k_render_num_modes];
  struct render_table_2MHz render_table_mode0;
  struct render_table_2MHz render_table_mode1;
  struct render_table_2MHz render_table_mode2;
  struct render_table_1MHz render_table_mode4;
  struct render_table_1MHz render_table_mode5;

  struct render_character_1MHz render_character_1MHz_white;
  struct render_character_1MHz render_character_1MHz_black;
  struct render_character_2MHz render_character_2MHz_white;
  struct render_character_2MHz render_character_2MHz_black;

  struct render_table_1MHz* p_render_table_1MHz;
  struct render_table_2MHz* p_render_table_2MHz;
  void (*func_render_data)(struct render_struct*, uint8_t);
  void (*func_render_blank)(struct render_struct*, uint8_t);

  int render_mode;
  uint32_t pixels_size;
  uint32_t horiz_beam_pos;
  uint32_t vert_beam_pos;
  uint32_t horiz_beam_window_start_pos;
  uint32_t horiz_beam_window_end_pos;
  uint32_t vert_beam_window_start_pos;
  uint32_t vert_beam_window_end_pos;
  uint32_t* p_render_pos;
  uint32_t* p_render_pos_row;
  uint32_t* p_render_pos_row_max;
  int do_interlace_wobble;
  int do_skip_next_hsync_vert_pos;
  int do_show_frame_boundaries;
};

static void
render_dirty_all_tables(struct render_struct* p_render) {
  uint32_t i;
  for (i = 0; i < k_render_num_modes; ++i) {
    p_render->render_table_dirty[i] = 1;
  }
}

struct render_struct*
render_create(struct bbc_options* p_options) {
  uint32_t width;
  uint32_t height;
  uint32_t i;

  uint32_t border_chars = 4;

  /* These numbers, 15 and 4, come from the delta between horiz/vert sync
   * position and of line/frame, in a standard MODE.
   * Note that AUG states R7 as 34 for a lot of the screen modes whereas it
   * should be 35!
   */
  uint32_t k_horiz_standard_offset = 15;
  uint32_t k_vert_standard_offset = 4;

  struct render_struct* p_render = malloc(sizeof(struct render_struct));
  if (p_render == NULL) {
    errx(1, "cannot allocate render_struct");
  }

  /* Also sets the palette to black. */
  (void) memset(p_render, '\0', sizeof(struct render_struct));

  /* "border characters" is the number of MODE1 square 8x8 pixel characters
   * used to pad the display window beyond the standard viewport for MODE1.
   * If set to zero, standard modes will fit perfectly. If set larger than
   * zero, pixels rendered in "overscan" areas will start to be visible.
   */
  (void) util_get_u32_option(&border_chars,
                             p_options->p_opt_flags,
                             "video:border-chars=");
  if (border_chars > 16) {
    errx(1, "border-chars must be 16 or less");
  }

  p_render->do_interlace_wobble = util_has_option(p_options->p_opt_flags,
                                                  "video:interlace-wobble");
  p_render->do_skip_next_hsync_vert_pos = 0;

  p_render->do_show_frame_boundaries = util_has_option(
      p_options->p_opt_flags, "video:frame-boundaries");

  width = (640 + (border_chars * 2 * 16));
  height = (512 + (border_chars * 2 * 16));

  p_render->width = width;
  p_render->height = height;

  if (border_chars > k_horiz_standard_offset) {
    p_render->horiz_beam_window_start_pos = 0;
  } else {
    p_render->horiz_beam_window_start_pos =
        ((k_horiz_standard_offset - border_chars) * 16);
  }
  p_render->horiz_beam_window_end_pos = (p_render->width +
                                         p_render->horiz_beam_window_start_pos);

  if (border_chars > k_vert_standard_offset) {
    p_render->vert_beam_window_start_pos = 0;
  } else {
    p_render->vert_beam_window_start_pos =
        ((k_vert_standard_offset - border_chars) * 16);
  }
  p_render->vert_beam_window_end_pos = (p_render->height +
                                        p_render->vert_beam_window_start_pos);

  p_render->render_mode = k_render_mode0;
  p_render->pixels_size = 8;

  render_dirty_all_tables(p_render);

  for (i = 0; i < 16; ++i) {
    p_render->render_character_1MHz_white.host_pixels[i] = 0xffffffff;
    p_render->render_character_1MHz_black.host_pixels[i] = 0xff000000;
  }
  for (i = 0; i < 8; ++i) {
    p_render->render_character_2MHz_white.host_pixels[i] = 0xffffffff;
    p_render->render_character_2MHz_black.host_pixels[i] = 0xff000000;
  }

  return p_render;
}

void
render_destroy(struct render_struct* p_render) {
  free(p_render);
}

uint32_t
render_get_width(struct render_struct* p_render) {
  return p_render->width;
}

uint32_t
render_get_height(struct render_struct* p_render) {
  return p_render->height;
}

uint32_t*
render_get_buffer(struct render_struct* p_render) {
  return p_render->p_buffer;
}

void
render_set_buffer(struct render_struct* p_render, uint32_t* p_buffer) {
  assert(p_render->p_buffer == NULL);
  p_render->p_buffer = p_buffer;
  p_render->p_buffer_end = p_buffer;
  p_render->p_buffer_end += (p_render->width * p_render->height);

  render_clear_buffer(p_render);

  /* These reset p_render_pos. */
  render_hsync(p_render);
  render_vsync(p_render);
}

static void
render_function_null(struct render_struct* p_render, uint8_t data) {
  (void) p_render;
  (void) data;
}

static inline void
render_reset_render_pos(struct render_struct* p_render) {
  uint32_t window_horiz_pos;
  uint32_t window_vert_pos;

  p_render->p_render_pos = p_render->p_buffer_end;
  p_render->p_render_pos_row = p_render->p_buffer_end;

  if (p_render->vert_beam_pos >= p_render->vert_beam_window_end_pos) {
    return;
  }
  if (p_render->vert_beam_pos < p_render->vert_beam_window_start_pos) {
    return;
  }

  window_vert_pos = (p_render->vert_beam_pos -
                     p_render->vert_beam_window_start_pos);
  p_render->p_render_pos_row = p_render->p_buffer;
  p_render->p_render_pos_row += (window_vert_pos * p_render->width);

  if (p_render->horiz_beam_pos >= p_render->horiz_beam_window_end_pos) {
    return;
  }
  if (p_render->horiz_beam_pos < p_render->horiz_beam_window_start_pos) {
    return;
  }

  window_horiz_pos = (p_render->horiz_beam_pos -
                      p_render->horiz_beam_window_start_pos);

  p_render->p_render_pos = p_render->p_render_pos_row;
  p_render->p_render_pos += window_horiz_pos;
  p_render->p_render_pos_row_max = (p_render->p_render_pos +
                                    p_render->width -
                                    p_render->pixels_size);
}

static void
render_function_1MHz_data(struct render_struct* p_render, uint8_t data) {
  uint32_t* p_render_pos = p_render->p_render_pos;
  struct render_character_1MHz* p_character =
      (struct render_character_1MHz*) p_render_pos;

  p_render->horiz_beam_pos += 16;

  if (p_render_pos <= p_render->p_render_pos_row_max) {
    *p_character = p_render->p_render_table_1MHz->values[data];
    p_render->p_render_pos += 16;
  } else if (p_render->horiz_beam_pos ==
             p_render->horiz_beam_window_start_pos) {
    render_reset_render_pos(p_render);
  }
}

static void
render_function_1MHz_blank(struct render_struct* p_render, uint8_t data) {
  uint32_t* p_render_pos = p_render->p_render_pos;
  struct render_character_1MHz* p_character =
      (struct render_character_1MHz*) p_render_pos;

  (void) data;
  /* TODO: If we're maintaining the canvas background correctly as black, we
   * should skip painting anything and just advance the pointers.
   */
  p_render->horiz_beam_pos += 16;

  if (p_render_pos <= p_render->p_render_pos_row_max) {
    *p_character = p_render->render_character_1MHz_black;
    p_render->p_render_pos += 16;
  } else if (p_render->horiz_beam_pos ==
             p_render->horiz_beam_window_start_pos) {
    render_reset_render_pos(p_render);
  }
}

static void
render_function_2MHz_data(struct render_struct* p_render, uint8_t data) {
  uint32_t* p_render_pos = p_render->p_render_pos;
  struct render_character_2MHz* p_character =
      (struct render_character_2MHz*) p_render_pos;

  p_render->horiz_beam_pos += 8;

  if (p_render_pos <= p_render->p_render_pos_row_max) {
    *p_character = p_render->p_render_table_2MHz->values[data];
    p_render->p_render_pos += 8;
  } else if (p_render->horiz_beam_pos ==
             p_render->horiz_beam_window_start_pos) {
    render_reset_render_pos(p_render);
  }
}

static void
render_function_2MHz_blank(struct render_struct* p_render, uint8_t data) {
  uint32_t* p_render_pos = p_render->p_render_pos;
  struct render_character_2MHz* p_character =
      (struct render_character_2MHz*) p_render_pos;

  (void) data;

  p_render->horiz_beam_pos += 8;

  if (p_render_pos <= p_render->p_render_pos_row_max) {
    *p_character = p_render->render_character_2MHz_black;
    p_render->p_render_pos += 8;
  } else if (p_render->horiz_beam_pos ==
             p_render->horiz_beam_window_start_pos) {
    render_reset_render_pos(p_render);
  }
}

void
render_set_mode(struct render_struct* p_render, int mode) {
  assert((mode >= k_render_mode0) && (mode <= k_render_mode7));
  switch (mode) {
  case k_render_mode0:
  case k_render_mode1:
  case k_render_mode2:
    p_render->func_render_data = render_function_2MHz_data;
    p_render->func_render_blank = render_function_2MHz_blank;
    p_render->p_render_table_2MHz = render_get_2MHz_render_table(p_render,
                                                                 mode);
    p_render->pixels_size = 8;
    break;
  case k_render_mode4:
  case k_render_mode5:
    p_render->func_render_data = render_function_1MHz_data;
    p_render->func_render_blank = render_function_1MHz_blank;
    p_render->p_render_table_1MHz = render_get_1MHz_render_table(p_render,
                                                                 mode);
    p_render->pixels_size = 16;
    break;
  case k_render_mode7:
    p_render->func_render_data = render_function_null;
    p_render->func_render_blank = render_function_null;
    p_render->p_render_table_1MHz = NULL;
    p_render->pixels_size = 16;
    break;
  default:
    assert(0);
    break;
  }

  p_render->render_mode = mode;

  /* Changing 1MHz <-> 2MHz changes the size of the pixel blocks we write, and
   * therefore the bounds.
   */
  render_reset_render_pos(p_render);
}

void
render_set_palette(struct render_struct* p_render,
                   uint8_t index,
                   uint32_t rgba) {
  p_render->palette[index] = rgba;
  render_dirty_all_tables(p_render);
}

static void
render_generate_mode0_table(struct render_struct* p_render) {
  uint32_t i;

  struct render_table_2MHz* p_table = &p_render->render_table_mode0;

  /* TODO: looks like we only support a black and white MODE0?? */
  for (i = 0; i < 256; ++i) {
    uint8_t p0 = !!(i & 0x80);
    uint8_t p1 = !!(i & 0x40);
    uint8_t p2 = !!(i & 0x20);
    uint8_t p3 = !!(i & 0x10);
    uint8_t p4 = !!(i & 0x08);
    uint8_t p5 = !!(i & 0x04);
    uint8_t p6 = !!(i & 0x02);
    uint8_t p7 = !!(i & 0x01);

    struct render_character_2MHz* p_character = &p_table->values[i];
    p_character->host_pixels[0] = ~(p0 - 1);
    p_character->host_pixels[1] = ~(p1 - 1);
    p_character->host_pixels[2] = ~(p2 - 1);
    p_character->host_pixels[3] = ~(p3 - 1);
    p_character->host_pixels[4] = ~(p4 - 1);
    p_character->host_pixels[5] = ~(p5 - 1);
    p_character->host_pixels[6] = ~(p6 - 1);
    p_character->host_pixels[7] = ~(p7 - 1);
  }
}

static void
render_generate_mode1_table(struct render_struct* p_render) {
  uint32_t i;

  struct render_table_2MHz* p_table = &p_render->render_table_mode1;

  for (i = 0; i < 256; ++i) {
    uint8_t v0 = (((i & 0x80) >> 6) | ((i & 0x08) >> 3));
    uint8_t v1 = (((i & 0x40) >> 5) | ((i & 0x04) >> 2));
    uint8_t v2 = (((i & 0x20) >> 4) | ((i & 0x02) >> 1));
    uint8_t v3 = (((i & 0x10) >> 3) | ((i & 0x01) >> 0));
    uint32_t p0 = p_render->palette[(4 + (v0 << 1))];
    uint32_t p1 = p_render->palette[(4 + (v1 << 1))];
    uint32_t p2 = p_render->palette[(4 + (v2 << 1))];
    uint32_t p3 = p_render->palette[(4 + (v3 << 1))];

    struct render_character_2MHz* p_character = &p_table->values[i];
    p_character->host_pixels[0] = p0;
    p_character->host_pixels[1] = p0;
    p_character->host_pixels[2] = p1;
    p_character->host_pixels[3] = p1;
    p_character->host_pixels[4] = p2;
    p_character->host_pixels[5] = p2;
    p_character->host_pixels[6] = p3;
    p_character->host_pixels[7] = p3;
  }
}

static void
render_generate_mode2_table(struct render_struct* p_render) {
  uint32_t i;

  struct render_table_2MHz* p_table = &p_render->render_table_mode2;

  for (i = 0; i < 256; ++i) {
    uint8_t v0 = ((i & 0x80) >> 4) |
                  ((i & 0x20) >> 3) |
                  ((i & 0x08) >> 2) |
                  ((i & 0x02) >> 1);
    uint8_t v1 = ((i & 0x40) >> 3) |
                  ((i & 0x10) >> 2) |
                  ((i & 0x04) >> 1) |
                  ((i & 0x01) >> 0);
    uint32_t p0 = p_render->palette[v0];
    uint32_t p1 = p_render->palette[v1];

    struct render_character_2MHz* p_character = &p_table->values[i];
    p_character->host_pixels[0] = p0;
    p_character->host_pixels[1] = p0;
    p_character->host_pixels[2] = p0;
    p_character->host_pixels[3] = p0;
    p_character->host_pixels[4] = p1;
    p_character->host_pixels[5] = p1;
    p_character->host_pixels[6] = p1;
    p_character->host_pixels[7] = p1;
  }
}

static void
render_generate_mode4_table(struct render_struct* p_render) {
  uint32_t i;

  struct render_table_1MHz* p_table = &p_render->render_table_mode4;

  /* TODO: is there some commonality we can factor out with MODE0? */
  for (i = 0; i < 256; ++i) {
    uint32_t p0 = p_render->palette[((i & 0x80) >> 4)];
    uint32_t p1 = p_render->palette[((i & 0x40) >> 3)];
    uint32_t p2 = p_render->palette[((i & 0x20) >> 2)];
    uint32_t p3 = p_render->palette[((i & 0x10) >> 1)];
    uint32_t p4 = p_render->palette[(i & 0x08)];
    uint32_t p5 = p_render->palette[((i & 0x04) << 1)];
    uint32_t p6 = p_render->palette[((i & 0x02) << 2)];
    uint32_t p7 = p_render->palette[((i & 0x01) << 3)];

    struct render_character_1MHz* p_character = &p_table->values[i];
    p_character->host_pixels[0] = p0;
    p_character->host_pixels[1] = p0;
    p_character->host_pixels[2] = p1;
    p_character->host_pixels[3] = p1;
    p_character->host_pixels[4] = p2;
    p_character->host_pixels[5] = p2;
    p_character->host_pixels[6] = p3;
    p_character->host_pixels[7] = p3;
    p_character->host_pixels[8] = p4;
    p_character->host_pixels[9] = p4;
    p_character->host_pixels[10] = p5;
    p_character->host_pixels[11] = p5;
    p_character->host_pixels[12] = p6;
    p_character->host_pixels[13] = p6;
    p_character->host_pixels[14] = p7;
    p_character->host_pixels[15] = p7;
  }
}

static void
render_generate_mode5_table(struct render_struct* p_render) {
  uint32_t i;

  struct render_table_1MHz* p_table = &p_render->render_table_mode5;

  /* TODO: is there some commonality we can factor out with MODE1? */
  for (i = 0; i < 256; ++i) {
    uint8_t v0 = (((i & 0x80) >> 6) | ((i & 0x08) >> 3));
    uint8_t v1 = (((i & 0x40) >> 5) | ((i & 0x04) >> 2));
    uint8_t v2 = (((i & 0x20) >> 4) | ((i & 0x02) >> 1));
    uint8_t v3 = (((i & 0x10) >> 3) | ((i & 0x01) >> 0));
    uint32_t p0 = p_render->palette[(4 + (v0 << 1))];
    uint32_t p1 = p_render->palette[(4 + (v1 << 1))];
    uint32_t p2 = p_render->palette[(4 + (v2 << 1))];
    uint32_t p3 = p_render->palette[(4 + (v3 << 1))];

    struct render_character_1MHz* p_character = &p_table->values[i];
    p_character->host_pixels[0] = p0;
    p_character->host_pixels[1] = p0;
    p_character->host_pixels[2] = p0;
    p_character->host_pixels[3] = p0;
    p_character->host_pixels[4] = p1;
    p_character->host_pixels[5] = p1;
    p_character->host_pixels[6] = p1;
    p_character->host_pixels[7] = p1;
    p_character->host_pixels[8] = p2;
    p_character->host_pixels[9] = p2;
    p_character->host_pixels[10] = p2;
    p_character->host_pixels[11] = p2;
    p_character->host_pixels[12] = p3;
    p_character->host_pixels[13] = p3;
    p_character->host_pixels[14] = p3;
    p_character->host_pixels[15] = p3;
  }
}

struct render_table_2MHz* render_get_2MHz_render_table(
    struct render_struct* p_render, int mode) {
  if (p_render->render_table_dirty[mode]) {
    switch (mode) {
    case k_render_mode0:
      render_generate_mode0_table(p_render);
      break;
    case k_render_mode1:
      render_generate_mode1_table(p_render);
      break;
    case k_render_mode2:
      render_generate_mode2_table(p_render);
      break;
    default:
      assert(0);
      break;
    }
    p_render->render_table_dirty[mode] = 0;
  }

  switch (mode) {
  case k_render_mode0:
    return &p_render->render_table_mode0;
  case k_render_mode1:
    return &p_render->render_table_mode1;
  case k_render_mode2:
    return &p_render->render_table_mode2;
  default:
    assert(0);
    return NULL;
  }
}

struct render_table_1MHz* render_get_1MHz_render_table(
    struct render_struct* p_render, int mode) {
  if (p_render->render_table_dirty[mode]) {
    switch (mode) {
    case k_render_mode4:
      render_generate_mode4_table(p_render);
      break;
    case k_render_mode5:
      render_generate_mode5_table(p_render);
      break;
    default:
      assert(0);
      break;
    }
    p_render->render_table_dirty[mode] = 0;
  }

  switch (mode) {
  case k_render_mode4:
    return &p_render->render_table_mode4;
  case k_render_mode5:
    return &p_render->render_table_mode5;
  default:
    assert(0);
    return NULL;
  }
}

void (*render_get_render_data_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t) {
  int render_mode = p_render->render_mode;
  if (p_render->render_table_dirty[render_mode]) {
    render_set_mode(p_render, render_mode);
  }
  return p_render->func_render_data;
}

void (*render_get_render_blank_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t) {
  return p_render->func_render_blank;
}

void
render_clear_buffer(struct render_struct* p_render) {
  uint32_t size = (p_render->width * p_render->height * 4);
  (void) memset(p_render->p_buffer, '\0', size);
}

void
render_double_up_lines(struct render_struct* p_render) {
  /* TODO: only need to double up partial lines within the render border. */
  uint32_t line;

  uint32_t lines = (p_render->height / 2);
  uint32_t width = p_render->width;
  uint32_t double_width = (width * 2);
  uint32_t* p_buffer = p_render->p_buffer;
  uint32_t* p_buffer_next_line = (p_buffer + width);

  for (line = 0; line < lines; ++line) {
    (void) memcpy(p_buffer_next_line, p_buffer, (width * 4));
    p_buffer += double_width;
    p_buffer_next_line += double_width;
  }
}

void
render_hsync(struct render_struct* p_render) {
  p_render->horiz_beam_pos = 0;
  if (!p_render->do_skip_next_hsync_vert_pos) {
    p_render->vert_beam_pos += 2;
  } else {
    p_render->do_skip_next_hsync_vert_pos = 0;
  }
  /* TODO: do a vertical flyback if beam pos gets too low. */
  render_reset_render_pos(p_render);
}

void
render_vsync(struct render_struct* p_render) {
  p_render->vert_beam_pos = 0;
  if (!p_render->do_interlace_wobble && (p_render->horiz_beam_pos >= 512)) {
    /* TODO: the interlace wobble, if enabled, is wobbling too much. It wobbles
     * 1 full vertical scanline (2 host pixels) instead of a half scanline.
     */
    p_render->do_skip_next_hsync_vert_pos = 1;
  }
  render_reset_render_pos(p_render);
}

void
render_frame_boundary(struct render_struct* p_render) {
  uint32_t i;

  if (!p_render->do_show_frame_boundaries) {
    return;
  }
  if (p_render->p_render_pos_row == p_render->p_buffer_end) {
    return;
  }

  /* Paint a red line to edge of canvas denote CRTC frame boundary. */
  for (i = 0; i < p_render->width; ++i) {
    p_render->p_render_pos_row[i] = 0xffff0000;
  }
}
