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
#include "util.h"
#include "video.h"

#include <string.h>

/* Used for stretching 6 pixels wide into 16. */
static const uint8_t k_stretch_data[] = {
  0, 255, 0, 0,
  0, 255, 0, 0,
  0, 170, 1, 85,
  1, 255, 0, 0,
  1, 255, 0, 0,
  1, 85,  2, 170,
  2, 255, 0, 0,
  2, 255, 0, 0,

  3, 255, 0, 0,
  3, 255, 0, 0,
  3, 170, 4, 85,
  4, 255, 0, 0,
  4, 255, 0, 0,
  4, 85,  5, 170,
  5, 255, 0, 0,
  5, 255, 0, 0,
};

static int s_teletext_was_generated;

static uint8_t s_teletext_generated_gfx[96 * 60];
static uint8_t s_teletext_generated_sep_gfx[96 * 60];

struct teletext_struct {
  uint32_t palette[8];
  uint32_t flash_count;
  int flash_visible_this_frame;
  uint32_t scanline;
  uint8_t* p_active_characters;
  int is_graphics_active;
  int is_separated_active;
  int double_active;
  int flash_active;
  int had_double_active_this_scanline;
  int second_character_row_of_double;
  uint32_t fg_color;
  uint32_t bg_color;
  int is_hold_graphics;
  uint8_t* p_held_character;
};

static void
teletext_draw_block(uint8_t* p_glyph,
                    uint32_t x,
                    uint32_t y,
                    uint32_t w,
                    uint32_t h) {
  uint32_t i_width;
  uint32_t i_height;

  for (i_height = 0; i_height < h; ++i_height) {
    for (i_width = 0; i_width < w; ++i_width) {
      p_glyph[((y + i_height) * 6) + x + i_width] = 1;
    }
  }
}

static void
teletext_generate() {
  uint32_t i;

  s_teletext_was_generated = 1;

  /* 0x40 - 0x5F in graphics modes use the character glyph. */
  (void) memcpy(&s_teletext_generated_gfx[(0x40 - 0x20) * 60],
                &teletext_characters[(0x40 - 0x20) * 60],
                (0x20 * 60));
  (void) memcpy(&s_teletext_generated_sep_gfx[(0x40 - 0x20) * 60],
                &teletext_characters[(0x40 - 0x20) * 60],
                (0x20 * 60));

  for (i = 0; i < 0x40; ++i) {
    uint8_t glyph_index;
    uint8_t* p_gfx_dest;
    uint8_t* p_sep_gfx_dest;

    glyph_index = i;
    if (i >= 0x20) {
      glyph_index += 0x20;
    }
    p_gfx_dest = &s_teletext_generated_gfx[glyph_index * 60];
    p_sep_gfx_dest = &s_teletext_generated_sep_gfx[glyph_index * 60];

    if (i & 0x01) {
      teletext_draw_block(p_gfx_dest, 0, 0, 3, 3);
      teletext_draw_block(p_sep_gfx_dest, 1, 0, 2, 2);
    }
    if (i & 0x02) {
      teletext_draw_block(p_gfx_dest, 3, 0, 3, 3);
      teletext_draw_block(p_sep_gfx_dest, 4, 0, 2, 2);
    }
    if (i & 0x04) {
      teletext_draw_block(p_gfx_dest, 0, 3, 3, 4);
      teletext_draw_block(p_sep_gfx_dest, 1, 3, 2, 3);
    }
    if (i & 0x08) {
      teletext_draw_block(p_gfx_dest, 3, 3, 3, 4);
      teletext_draw_block(p_sep_gfx_dest, 4, 3, 2, 3);
    }
    if (i & 0x10) {
      teletext_draw_block(p_gfx_dest, 0, 7, 3, 3);
      teletext_draw_block(p_sep_gfx_dest, 1, 7, 2, 2);
    }
    if (i & 0x20) {
      teletext_draw_block(p_gfx_dest, 3, 7, 3, 3);
      teletext_draw_block(p_sep_gfx_dest, 4, 7, 2, 2);
    }
  }
}

static inline void
teletext_set_active_characters(struct teletext_struct* p_teletext) {
  if (p_teletext->is_graphics_active) {
    if (p_teletext->is_separated_active) {
      p_teletext->p_active_characters = &s_teletext_generated_sep_gfx[0];
    } else {
      p_teletext->p_active_characters = &s_teletext_generated_gfx[0];
    }
  } else {
    p_teletext->p_active_characters = &teletext_characters[0];
  }
}

static inline void
teletext_scanline_ended(struct teletext_struct* p_teletext) {
  p_teletext->is_graphics_active = 0;
  p_teletext->is_separated_active = 0;
  p_teletext->double_active = 0;
  p_teletext->flash_active = 0;
  p_teletext->fg_color = p_teletext->palette[7];
  p_teletext->bg_color = p_teletext->palette[0];
  p_teletext->is_hold_graphics = 0;
  /* This is space. */
  p_teletext->p_held_character = &teletext_characters[0];

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

static inline void
teletext_new_frame_started(struct teletext_struct* p_teletext) {
  p_teletext->scanline = 0;

  p_teletext->flash_count++;
  if (p_teletext->flash_count == 48) {
    p_teletext->flash_count = 0;
  }
  p_teletext->flash_visible_this_frame = (p_teletext->flash_count >= 16);
}

struct teletext_struct*
teletext_create() {
  uint32_t i;
  struct teletext_struct* p_teletext;

  (void) teletext_graphics;
  (void) teletext_separated_graphics;
  if (!s_teletext_was_generated) {
    teletext_generate();
  }

  p_teletext = util_mallocz(sizeof(struct teletext_struct));

  p_teletext->flash_count = 0;
  p_teletext->scanline = 0;

  teletext_scanline_ended(p_teletext);
  teletext_new_frame_started(p_teletext);

  for (i = 0; i < 8; ++i) {
    uint32_t color = 0;
    if (i & 1) {
      color |= 0x010000;
    }
    if (i & 2) {
      color |= 0x000100;
    }
    if (i & 4) {
      color |= 0x000001;
    }
    p_teletext->palette[i] = color;
  }

  return p_teletext;
}

void
teletext_destroy(struct teletext_struct* p_teletext) {
  util_free(p_teletext);
}

static inline void
teletext_handle_control_character(struct teletext_struct* p_teletext,
                                  uint32_t* p_fg_color,
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
    p_teletext->is_graphics_active = 0;
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
    p_teletext->is_graphics_active = 1;
    p_teletext->fg_color = p_teletext->palette[(src_char & 7)];
    break;
  case 24:
    /* Not commonly seen but needed e.g. by the JCB Digger MODE7 intro
     * animation.
     */
    p_teletext->fg_color = p_teletext->bg_color;
    /* This control code is set-at, unlike other changes to foreground color. */
    *p_fg_color = p_teletext->fg_color;
    break;
  case 25:
    p_teletext->is_separated_active = 0;
    break;
  case 26:
    p_teletext->is_separated_active = 1;
    break;
  case 28:
    p_teletext->bg_color = p_teletext->palette[0];
    break;
  case 29:
    p_teletext->bg_color = p_teletext->fg_color;
    break;
  case 30:
    p_teletext->is_hold_graphics = 1;
    break;
  case 31:
    p_teletext->is_hold_graphics = 0;
    break;
  }

  teletext_set_active_characters(p_teletext);
}

void
teletext_render_data(struct teletext_struct* p_teletext,
                     struct render_character_1MHz* p_out,
                     uint8_t data) {
  uint32_t i;
  uint32_t j;
  uint32_t bg_color;

  /* Foreground color and active characters are set-after so load them before
   * potentially processing a control code.
   */
  uint32_t fg_color = p_teletext->fg_color;
  int is_hold_graphics = p_teletext->is_hold_graphics;
  /* Selects space, 0x20. */
  uint8_t* p_src_data = p_teletext->p_active_characters;
  uint32_t src_data_scanline = p_teletext->scanline;

  data &= 0x7F;

  if (data >= 0x20) {
    p_src_data += (60 * (data - 0x20));
    /* EMU NOTE: from the Teletext spec, "the "Held-Mosaic" character inserted
     * is the most recent mosaics character with bit 6 = '1' in its code on
     * that row".
     * This mostly matches the chip in the beeb, but one notable exception is
     * that control codes _outside_ of hold graphics reset the held character
     * to space.
     * Also: "The "Held-Mosaic" character is reset to "SPACE" at the start of
     * each row, on a change of alphanumeric/mosaics mode..."
     */
    if (p_teletext->is_graphics_active) {
      if (data & 0x20) {
        p_teletext->p_held_character = p_src_data;
      }
    } else {
      p_teletext->p_held_character = &teletext_characters[0];
    }
  } else {
    uint8_t* p_held_character = p_teletext->p_held_character;
    int is_graphics_active = p_teletext->is_graphics_active;

    teletext_handle_control_character(p_teletext, &fg_color, data);
    /* Hold on is set-at and hold off is set-after. */
    is_hold_graphics |= p_teletext->is_hold_graphics;
    if (is_graphics_active && is_hold_graphics) {
      p_src_data = p_held_character;
    } else {
      p_teletext->p_held_character = &teletext_characters[0];
    }
  }

  if (p_out == NULL) {
    return;
  }

  if ((p_teletext->flash_active && !p_teletext->flash_visible_this_frame) ||
      (p_teletext->second_character_row_of_double &&
       !p_teletext->double_active)) {
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

  /* NOTE: would be nice to pre-calculate some of this but there are a huge
   * number of glyph + color combinations.
   */
  j = 0;
  for (i = 0; i < 16; ++i) {
    uint32_t color;
    uint8_t p1 = p_src_data[k_stretch_data[j]];
    uint8_t p2 = p_src_data[k_stretch_data[j + 2]];
    uint32_t c1 = (p1 ? fg_color : bg_color);
    uint32_t c2 = (p2 ? fg_color : bg_color);

    color = (c1 * k_stretch_data[j + 1]);
    color += (c2 * k_stretch_data[j + 3]);

    p_out->host_pixels[i] = (color | 0xff000000);

    j += 4;
  }
}

void
teletext_DISPMTG_changed(struct teletext_struct* p_teletext, int value) {
  /* TODO: we've currently only wired this up to HSYNC but it will suffice. */
  (void) value;

  teletext_scanline_ended(p_teletext);
}

void
teletext_VSYNC_changed(struct teletext_struct* p_teletext, int value) {
  /* TODO: we've currently only wired this up to VSYNC lower but it will
   * suffice.
   */
  (void) value;

  teletext_new_frame_started(p_teletext);
}
