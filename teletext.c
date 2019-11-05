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

#include "video.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>

struct teletext_struct {
  uint32_t palette[8];
  uint32_t scanline;
  uint8_t* p_active_characters;
  int graphics_active;
  int separated_active;
  uint32_t fg_color;
  uint32_t bg_color;
};

struct teletext_struct*
teletext_create() {
  uint32_t i;

  struct teletext_struct* p_teletext = malloc(sizeof(struct teletext_struct));
  if (p_teletext == NULL) {
    errx(1, "cannot allocate teletext_struct");
  }

  (void) memset(p_teletext, '\0', sizeof(struct teletext_struct));

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

void
teletext_scanline_ended(struct teletext_struct* p_teletext) {
  p_teletext->graphics_active = 0;
  p_teletext->separated_active = 0;
  p_teletext->fg_color = p_teletext->palette[7];
  p_teletext->bg_color = p_teletext->palette[0];

  teletext_set_active_characters(p_teletext);
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
                     uint32_t scanline,
                     uint32_t* p_dest_buffer) {
  uint32_t column;
  uintptr_t src_chars = (uintptr_t) p_src_chars;

  teletext_scanline_ended(p_teletext);

  for (column = 0; column < 40; ++column) {
    uint8_t src_char;
    uint32_t value;
    uint32_t bg_color;
    uint32_t fg_color;
    /* Selects space, 0x20. */
    uint8_t* p_src_data = p_teletext->p_active_characters;

    /* TODO: can abstract this. */
    if (src_chars & 0x8000) {
      src_chars &= ~0x8000;
      src_chars |= 0x7C00;
    }
    src_char = ((*((uint8_t*) src_chars)) & 0x7F);
    src_chars++;

    if (src_char >= 0x20) {
      p_src_data += (60 * (src_char - 0x20));
    } else {
      teletext_handle_control_character(p_teletext, src_char);
    }
    p_src_data += (scanline * 6);

    bg_color = p_teletext->bg_color;
    fg_color = p_teletext->fg_color;

    *p_dest_buffer++ = bg_color;
    *p_dest_buffer++ = bg_color;
    value = (p_src_data[0] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = (p_src_data[1] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = (p_src_data[2] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = (p_src_data[3] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = (p_src_data[4] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = (p_src_data[5] ? fg_color : bg_color);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = bg_color;
    *p_dest_buffer++ = bg_color;
  }
}

void
teletext_render_full(struct teletext_struct* p_teletext,
                     struct video_struct* p_video,
                     uint32_t* p_buffer) {
  uint32_t row;
  uint32_t scanline;

  uint8_t* p_video_mem = video_get_memory(p_video);
  /* TODO: get stride from video. */
  size_t stride = 640;

  for (row = 0; row < 25; ++row) {
    p_teletext->scanline = 0;
    for (scanline = 0; scanline < 10; ++scanline) {
      teletext_render_line(p_teletext, p_video_mem, scanline, p_buffer);
      p_buffer += stride;
      teletext_render_line(p_teletext, p_video_mem, scanline, p_buffer);
      p_buffer += stride;
    }
    p_video_mem += 40;
  }
}
