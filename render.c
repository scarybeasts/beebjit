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

  uint32_t palette[16];
  int render_table_dirty[k_render_num_modes];
  struct render_table_2MHz render_table_mode0;
  struct render_table_2MHz render_table_mode1;
  struct render_table_2MHz render_table_mode2;
  struct render_table_1MHz render_table_mode4;
  struct render_table_1MHz render_table_mode5;

  uint32_t horiz_beam_pos;
  uint32_t vert_beam_pos;
  int beam_is_in_bounds;
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

  uint32_t border_chars = 0;
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
                             "render:border-chars=");
  if (border_chars > 16) {
    errx(1, "border-chars must be 16 or less");
  }

  width = (640 + (border_chars * 16));
  height = (512 + (border_chars * 16));

  p_render->width = width;
  p_render->height = height;

  render_dirty_all_tables(p_render);

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

  render_clear_buffer(p_render);
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

static void
render_function_white(struct render_struct* p_render, uint8_t data) {
  (void) p_render;
  (void) data;
}

static void
render_function_black(struct render_struct* p_render, uint8_t data) {
  (void) p_render;
  (void) data;
}

void (*render_get_render_data_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t) {
  (void) p_render;
  return render_function_white;
}

void (*render_get_render_blank_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t) {
  (void) p_render;
  return render_function_black;
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
  p_render->vert_beam_pos += 2;
}

void
render_vsync(struct render_struct* p_render) {
  p_render->vert_beam_pos = 0;
}
