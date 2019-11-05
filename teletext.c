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
  uint32_t scanline;
};

struct teletext_struct*
teletext_create() {
  struct teletext_struct* p_teletext = malloc(sizeof(struct teletext_struct));
  if (p_teletext == NULL) {
    errx(1, "cannot allocate teletext_struct");
  }

  (void) memset(p_teletext, '\0', sizeof(struct teletext_struct));

  (void) teletext_characters;
  (void) teletext_graphics;
  (void) teletext_separated_graphics;

  return p_teletext;
}

void
teletext_destroy(struct teletext_struct* p_teletext) {
  free(p_teletext);
}

static void
teletext_render_line(uint8_t* p_src_chars,
                     uint32_t scanline,
                     uint32_t* p_dest_buffer) {
  uint32_t column;
  uintptr_t src_chars = (uintptr_t) p_src_chars;

  for (column = 0; column < 40; ++column) {
    uint8_t src_char;
    uint32_t value;
    uint32_t alpha = 0xff000000;
    uint8_t* p_src_data = &teletext_characters[0];

    /* TODO: can abstract this. */
    if (src_chars & 0x8000) {
      src_chars &= ~0x8000;
      src_chars |= 0x7C00;
    }
    src_char = ((*((uint8_t*) src_chars)) & 0x7F);
    src_chars++;

    /* Point at space (0x20). */
    if (src_char > 0x20) {
      p_src_data += (60 * (src_char - 0x20));
    }
    p_src_data += (scanline * 6);

    *p_dest_buffer++ = alpha;
    *p_dest_buffer++ = alpha;
    value = ((0x00ffffff * p_src_data[0]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = ((0x00ffffff * p_src_data[1]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = ((0x00ffffff * p_src_data[2]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = ((0x00ffffff * p_src_data[3]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = ((0x00ffffff * p_src_data[4]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    value = ((0x00ffffff * p_src_data[5]) | alpha);
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = value;
    *p_dest_buffer++ = alpha;
    *p_dest_buffer++ = alpha;
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
      teletext_render_line(p_video_mem, scanline, p_buffer);
      p_buffer += stride;
      teletext_render_line(p_video_mem, scanline, p_buffer);
      p_buffer += stride;
    }
    p_video_mem += 40;
  }
}
