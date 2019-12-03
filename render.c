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

  uint32_t horiz_beam_pos;
  uint32_t vertical_beam_pos;
  int beam_is_in_bounds;
};

struct render_struct*
render_create(struct bbc_options* p_options) {
  uint32_t width;
  uint32_t height;

  uint32_t border_chars = 0;
  struct render_struct* p_render = malloc(sizeof(struct render_struct));
  if (p_render == NULL) {
    errx(1, "cannot allocate render_struct");
  }

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

  return p_render;
}

void
render_destroy(struct render_struct* p_render) {
  free(p_render);
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

uint32_t
render_get_width(struct render_struct* p_render) {
  return p_render->width;
}

uint32_t
render_get_height(struct render_struct* p_render) {
  return p_render->height;
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
