#include "teletext.h"

/* Needed before teletext_glyphs.h */
#include <stdint.h>

/* Teletext raw glyph data.
 * With thanks,
 * from B-em v2.2. (Called bbctext.h there.)
 * GPL licensed.
 * (c) Sarah Walker.
 */
#include "teletext_glyphs.h"

#include "render.h"
#include "video.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>

struct teletext_struct {
  uint32_t palette[8];
  uint32_t flash_count;
  int flash_visible_this_frame;
  uint32_t scanline;
  uint8_t* p_active_characters;
  int graphics_active;
  int separated_active;
  int double_active;
  int flash_active;
  int had_double_active_this_scanline;
  int second_character_row_of_double;
  uint32_t fg_color;
  uint32_t bg_color;
};

static void
teletext_set_active_characters(struct teletext_struct* p_teletext) {
  if (p_teletext->graphics_active) {
    if (p_teletext->separated_active) {
      p_teletext->p_active_characters = &teletext_separated_graphics[0];
    } else {
      p_teletext->p_active_characters = &teletext_graphics[0];
    }
  } else {
    p_teletext->p_active_characters = &teletext_characters[0];
  }
}

static void
teletext_scanline_ended(struct teletext_struct* p_teletext) {
  p_teletext->graphics_active = 0;
  p_teletext->separated_active = 0;
  p_teletext->double_active = 0;
  p_teletext->flash_active = 0;
  p_teletext->fg_color = p_teletext->palette[7];
  p_teletext->bg_color = p_teletext->palette[0];

  teletext_set_active_characters(p_teletext);

  p_teletext->scanline++;
  if (p_teletext->scanline == 10) {
    p_teletext->scanline = 0;
    if (p_teletext->second_character_row_of_double) {
      p_teletext->second_character_row_of_double = 0;
    } else if (p_teletext->had_double_active_this_scanline) {
      p_teletext->second_character_row_of_double = 1;
    }
  }

  p_teletext->had_double_active_this_scanline = 0;
}

static void
teletext_frame_ended(struct teletext_struct* p_teletext) {
  p_teletext->flash_count++;
  if (p_teletext->flash_count == 48) {
    p_teletext->flash_count = 0;
  }
  p_teletext->flash_visible_this_frame = (p_teletext->flash_count >= 16);
}

struct teletext_struct*
teletext_create() {
  uint32_t i;

  struct teletext_struct* p_teletext = malloc(sizeof(struct teletext_struct));
  if (p_teletext == NULL) {
    errx(1, "cannot allocate teletext_struct");
  }

  (void) memset(p_teletext, '\0', sizeof(struct teletext_struct));

  teletext_scanline_ended(p_teletext);
  teletext_frame_ended(p_teletext);

  p_teletext->flash_count = 0;
  p_teletext->scanline = 0;

  for (i = 0; i < 8; ++i) {
    /* Black, full alpha. */
    uint32_t color = 0xff000000;
    if (i & 1) {
      color |= 0xff0000;
    }
    if (i & 2) {
      color |= 0xff00;
    }
    if (i & 4) {
      color |= 0xff;
    }
    p_teletext->palette[i] = color;
  }

  return p_teletext;
}

void
teletext_destroy(struct teletext_struct* p_teletext) {
  free(p_teletext);
}

static void
teletext_handle_control_character(struct teletext_struct* p_teletext,
                                  uint8_t src_char) {
  switch (src_char) {
  case 0:
    /* NOTE: SAA5050 appears to be a pre-2.5 presentation level, which doesn't
     * have the ability to select black.
     * See: https://www.etsi.org/deliver/etsi_i_ets/300700_300799/300706/01_60/ets_300706e01p.pdf
     */
    break;
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
    p_teletext->graphics_active = 0;
    p_teletext->fg_color = p_teletext->palette[src_char];
    break;
  case 8:
    p_teletext->flash_active = 1;
    break;
  case 9:
    p_teletext->flash_active = 0;
    break;
  case 12:
    p_teletext->double_active = 0;
    break;
  case 13:
    p_teletext->double_active = 1;
    p_teletext->had_double_active_this_scanline = 1;
    break;
  case 16:
    /* Can't select black graphics -- see above. */
    break;
  case 17:
  case 18:
  case 19:
  case 20:
  case 21:
  case 22:
  case 23:
    p_teletext->graphics_active = 1;
    p_teletext->fg_color = p_teletext->palette[(src_char & 7)];
    break;
  case 25:
    p_teletext->separated_active = 0;
    break;
  case 26:
    p_teletext->separated_active = 1;
    break;
  case 28:
    p_teletext->bg_color = p_teletext->palette[0];
    break;
  case 29:
    p_teletext->bg_color = p_teletext->fg_color;
    break;
  }

  teletext_set_active_characters(p_teletext);
}

static void
teletext_render_line(struct teletext_struct* p_teletext,
                     uint8_t* p_src_chars,
                     uint8_t columns,
                     uint8_t scanline,
                     uint32_t* p_dest_buffer) {
  uint32_t column;

  for (column = 0; column < columns; ++column) {
    uint32_t value;
    uint32_t bg_color;
    uint32_t fg_color;
    /* Selects space, 0x20. */
    uint8_t* p_src_data = p_teletext->p_active_characters;
    uint32_t src_data_scanline = scanline;
    uint8_t src_char = (*p_src_chars & 0x7F);

    p_src_chars++;

    if (src_char >= 0x20) {
      p_src_data += (60 * (src_char - 0x20));
    } else {
      teletext_handle_control_character(p_teletext, src_char);
    }

    if (p_teletext->flash_active && !p_teletext->flash_visible_this_frame) {
      /* Re-route to space. */
      p_src_data = &teletext_characters[0];
    }
    if (p_teletext->double_active) {
      src_data_scanline >>= 1;
      if (p_teletext->second_character_row_of_double) {
        src_data_scanline += 5;
      }
    }

    p_src_data += (src_data_scanline * 6);

    bg_color = p_teletext->bg_color;
    fg_color = p_teletext->fg_color;

    p_dest_buffer[0] = bg_color;
    p_dest_buffer[1] = bg_color;

    value = (p_src_data[0] ? fg_color : bg_color);
    p_dest_buffer[2] = value;
    p_dest_buffer[3] = value;

    value = (p_src_data[1] ? fg_color : bg_color);
    p_dest_buffer[4] = value;
    p_dest_buffer[5] = value;

    value = (p_src_data[2] ? fg_color : bg_color);
    p_dest_buffer[6] = value;
    p_dest_buffer[7] = value;

    value = (p_src_data[3] ? fg_color : bg_color);
    p_dest_buffer[8] = value;
    p_dest_buffer[9] = value;

    value = (p_src_data[4] ? fg_color : bg_color);
    p_dest_buffer[10] = value;
    p_dest_buffer[11] = value;

    value = (p_src_data[5] ? fg_color : bg_color);
    p_dest_buffer[12] = value;
    p_dest_buffer[13] = value;

    p_dest_buffer[14] = bg_color;
    p_dest_buffer[15] = bg_color;

    p_dest_buffer += 16;
  }
}

void
teletext_render_full(struct teletext_struct* p_teletext,
                     struct video_struct* p_video) {
  uint32_t row;
  uint32_t scanline;

  struct render_struct* p_render = video_get_render(p_video);
  uint32_t* p_render_buffer = render_get_buffer(p_render);
  uint32_t stride = render_get_width(p_render);
  uint32_t offset = 0;

  for (row = 0; row < 25; ++row) {
    uint8_t* p_video_mem = video_get_video_memory_slice(p_video,
                                                        offset,
                                                        40);

    p_teletext->scanline = 0;
    for (scanline = 0; scanline < 10; ++scanline) {
      teletext_render_line(p_teletext,
                           p_video_mem,
                           40,
                           scanline,
                           p_render_buffer);
      teletext_scanline_ended(p_teletext);

      p_render_buffer += (stride * 2);
    }

    offset += 40;
  }

  teletext_frame_ended(p_teletext);
}
