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

#include <assert.h>
#include <string.h>

/* Used for stretching 12 pixels wide into 16. */
static const uint8_t k_stretch_data[] = {
  0,  255, 0,  0,
  0,  85,  1,  170,
  1,  170, 2,  85,
  2,  255, 0,  0,

  3,  255, 0,  0,
  3,  85,  4,  170,
  4,  170, 5,  85,
  5,  255, 0,  0,

  6,  255, 0,  0,
  6,  85,  7,  170,
  7,  170, 8,  85,
  8,  255, 0,  0,

  9,  255, 0,  0,
  9,  85,  10, 170,
  10, 170, 11, 85,
  11, 255, 0,  0,
};

static int s_teletext_was_generated;

static uint8_t s_teletext_generated_glyphs[96 * 16 * 20];
static uint8_t s_teletext_generated_gfx[96 * 16 * 20];
static uint8_t s_teletext_generated_sep_gfx[96 * 16 * 20];


struct teletext_struct {
  struct render_character_1MHz render_character_1MHz_black;
  uint32_t background_color;
  uint8_t data_pipeline[3];
  uint8_t dispen_pipeline[2];
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
  int crtc_ra0;
  int is_isv;
  int curr_dispen;
  int incoming_dispen;
  uint8_t* p_render_character;
  uint32_t render_fg_color;
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
teletext_double_up_pixels(uint8_t* p_dest, uint8_t* p_src) {
  uint32_t x;
  uint32_t y;
  for (y = 0; y < 10; ++y) {
    for (x = 0; x < 6; ++x) {
      uint8_t* p_pixel_block = (p_dest + (y * 12 * 2) + (x * 2));
      uint8_t val = p_src[(y * 6) + x];
      *p_pixel_block = val;
      *(p_pixel_block + 1) = val;
      *(p_pixel_block + 12) = val;
      *(p_pixel_block + 12 + 1) = val;
    }
  }
}

static void
teletext_stretch_12_to_16(uint8_t* p_dest, uint8_t* p_src) {
  uint32_t x;
  uint32_t y;
  for (y = 0; y < 20; ++y) {
    const uint8_t* p_stretch_data = &k_stretch_data[0];
    for (x = 0; x < 16; ++x) {
      uint8_t val = (p_src[p_stretch_data[0]] * p_stretch_data[1]);
      val += (p_src[p_stretch_data[2]] * p_stretch_data[3]);
      *p_dest++ = val;
      p_stretch_data += 4;
    }
    p_src += 12;
  }
}

static void
teletext_smooth_diagonals(uint8_t* p_dest, uint8_t* p_src) {
  uint32_t x;
  uint32_t y;

  (void) memcpy(p_dest, p_src, (12 * 20));

  /* NOTE: could visit less rows / columns here but keeping it simple. */
  for (y = 1; y < 19; ++y) {
    for (x = 1; x < 11; ++x) {
      uint8_t* p_pixel = (p_src + (y * 12) + x);
      int up = *(p_pixel - 12);
      int down = *(p_pixel + 12);
      int left = *(p_pixel - 1);
      int right = *(p_pixel + 1);
      int down_left = *(p_pixel + 11);
      int up_right = *(p_pixel - 11);
      int down_right = *(p_pixel + 13);
      int up_left = *(p_pixel - 13);
      int smooth = 0;
      if (down && left && !down_left) {
        smooth = 1;
      }
      if (up && right && !up_right) {
        smooth = 1;
      }
      if (down && right && !down_right) {
        smooth = 1;
      }
      if (up && left && !up_left) {
        smooth = 1;
      }
      if (smooth) {
        p_dest[(y * 12) + x] = 1;
      }
    }
  }
}

static void
teletext_generate(void) {
  uint32_t i;

  s_teletext_was_generated = 1;

  /* Make the ROM glyphs pretty. */
  for (i = 0; i < 96; ++i) {
    uint8_t double_glyph[12 * 20];
    uint8_t pretty_glyph[12 * 20];
    uint8_t* p_glyph = &teletext_characters[i * 60];

    /* 6x10 to 12x20. */
    teletext_double_up_pixels(&double_glyph[0], p_glyph);

    /* Smooth the diagonals. */
    teletext_smooth_diagonals(&pretty_glyph[0], &double_glyph[0]);

    /* Stretch 12 pixels wide to 16, with anti-aliasing. */
    teletext_stretch_12_to_16(&s_teletext_generated_glyphs[i * 320],
                              &pretty_glyph[0]);
  }

  /* 0x40 - 0x5F in graphics modes use the character glyph. */
  (void) memcpy(&s_teletext_generated_gfx[(0x40 - 0x20) * 320],
                &s_teletext_generated_glyphs[(0x40 - 0x20) * 320],
                (0x20 * 320));
  (void) memcpy(&s_teletext_generated_sep_gfx[(0x40 - 0x20) * 320],
                &s_teletext_generated_glyphs[(0x40 - 0x20) * 320],
                (0x20 * 320));

  /* Generate the graphics glphys, which are on a 2x3 grid. */
  for (i = 0; i < 0x40; ++i) {
    uint8_t glyph_index;
    uint8_t gfx_buf[6 * 10];
    uint8_t sep_gfx_buf[6 * 10];
    uint8_t doubled_gfx_glyph[12 * 20];

    (void) memset(gfx_buf, '\0', sizeof(gfx_buf));
    (void) memset(sep_gfx_buf, '\0', sizeof(sep_gfx_buf));

    if (i & 0x01) {
      teletext_draw_block(&gfx_buf[0], 0, 0, 3, 3);
      teletext_draw_block(&sep_gfx_buf[0], 1, 0, 2, 2);
    }
    if (i & 0x02) {
      teletext_draw_block(&gfx_buf[0], 3, 0, 3, 3);
      teletext_draw_block(&sep_gfx_buf[0], 4, 0, 2, 2);
    }
    if (i & 0x04) {
      teletext_draw_block(&gfx_buf[0], 0, 3, 3, 4);
      teletext_draw_block(&sep_gfx_buf[0], 1, 3, 2, 3);
    }
    if (i & 0x08) {
      teletext_draw_block(&gfx_buf[0], 3, 3, 3, 4);
      teletext_draw_block(&sep_gfx_buf[0], 4, 3, 2, 3);
    }
    if (i & 0x10) {
      teletext_draw_block(&gfx_buf[0], 0, 7, 3, 3);
      teletext_draw_block(&sep_gfx_buf[0], 1, 7, 2, 2);
    }
    if (i & 0x20) {
      teletext_draw_block(&gfx_buf[0], 3, 7, 3, 3);
      teletext_draw_block(&sep_gfx_buf[0], 4, 7, 2, 2);
    }

    glyph_index = i;
    if (i >= 0x20) {
      glyph_index += 0x20;
    }

    teletext_double_up_pixels(&doubled_gfx_glyph[0], &gfx_buf[0]);
    teletext_stretch_12_to_16(&s_teletext_generated_gfx[glyph_index * 320],
                              &doubled_gfx_glyph[0]);

    teletext_double_up_pixels(&doubled_gfx_glyph[0], &sep_gfx_buf[0]);
    teletext_stretch_12_to_16(&s_teletext_generated_sep_gfx[glyph_index * 320],
                              &doubled_gfx_glyph[0]);
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
    p_teletext->p_active_characters = &s_teletext_generated_glyphs[0];
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
  p_teletext->p_held_character = &s_teletext_generated_glyphs[0];

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
  p_teletext->second_character_row_of_double = 0;

  p_teletext->flash_count++;
  if (p_teletext->flash_count == 48) {
    p_teletext->flash_count = 0;
  }
  p_teletext->flash_visible_this_frame = (p_teletext->flash_count >= 16);
}

static void
teletext_generate_black_character(struct teletext_struct* p_teletext) {
  uint32_t i;
  uint32_t rgba = (0xff000000 | p_teletext->background_color);

  for (i = 0; i < 16; ++i) {
    p_teletext->render_character_1MHz_black.host_pixels[i] = rgba;
  }

}

struct teletext_struct*
teletext_create(void) {
  uint32_t i;
  struct teletext_struct* p_teletext;

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

  p_teletext->background_color = 0;
  teletext_generate_black_character(p_teletext);

  return p_teletext;
}

void
teletext_destroy(struct teletext_struct* p_teletext) {
  util_free(p_teletext);
}

void
teletext_set_black_rgb(struct teletext_struct* p_teletext, uint32_t rgb) {
  p_teletext->background_color = rgb;
  teletext_generate_black_character(p_teletext);
}

static inline void
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
    p_teletext->render_fg_color = p_teletext->fg_color;
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

static inline void
teletext_do_data_byte(struct teletext_struct* p_teletext, uint8_t data) {
  /* Foreground color and active characters are set-after so load them before
   * potentially processing a control code.
   */
  int is_hold_graphics = p_teletext->is_hold_graphics;
  p_teletext->render_fg_color = p_teletext->fg_color;

  data &= 0x7F;

  if (data >= 0x20) {
    uint8_t* p_render_character = p_teletext->p_active_characters;
    p_render_character += (320 * (data - 0x20));
    p_teletext->p_render_character = p_render_character;
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
        p_teletext->p_held_character = p_render_character;
      }
    } else {
      p_teletext->p_held_character = &s_teletext_generated_glyphs[0];
    }
  } else {
    uint8_t* p_held_character = p_teletext->p_held_character;
    int is_graphics_active = p_teletext->is_graphics_active;
    int is_double_active = p_teletext->double_active;

    teletext_handle_control_character(p_teletext, data);
    /* Hold on is set-at and hold off is set-after. */
    is_hold_graphics |= p_teletext->is_hold_graphics;
    if (is_graphics_active &&
        is_hold_graphics &&
        (p_teletext->double_active == is_double_active)) {
      p_teletext->p_render_character = p_held_character;
    } else {
      p_teletext->p_render_character = &s_teletext_generated_glyphs[0];
      p_teletext->p_held_character = &s_teletext_generated_glyphs[0];
    }
  }
}

void
teletext_data(struct teletext_struct* p_teletext, uint8_t data) {
  /* This function handles the pipelining of incoming signals, which is to say
   * that in real hardware, incoming signals have their visible effect some
   * number of cycles after initial receipt.
   * There are two components potentially involved in the pipelining: the
   * IC15 latch (between the 6845 and SAA5050) and the SAA5050 chip itself.
   * In addition to the pipelining, the beeb teletext mode also uses 6845
   * "skew", which delays the 6845 DISPEN signal by 1 clock.
   * All together, the data bytes appear to hit the screen 3 clocks after the
   * 6845 selects them, and the display enable triggers after 1 clock of 6845
   * skew and 2 clocks of pipeline latency.
   */
  uint8_t queue_data = p_teletext->data_pipeline[0];
  int is_dispen = p_teletext->dispen_pipeline[0];

  /* A logic gate in IC37 and IC36 is used to set bit 6 in the data if DISPEN
   * is low. This has the effect of avoiding control codes.
   * We can get the same effect, a bit faster, by only sending data along if
   * display is enabled.
   */
  if (is_dispen) {
    teletext_do_data_byte(p_teletext, queue_data);
  }
  p_teletext->data_pipeline[0] = p_teletext->data_pipeline[1];
  p_teletext->data_pipeline[1] = p_teletext->data_pipeline[2];
  p_teletext->data_pipeline[2] = data;

  /* The chip increments its scanline counter on the falling edge of display
   * enable from the 6845.
   */
  if ((is_dispen == 0) && (p_teletext->curr_dispen == 1)) {
    teletext_scanline_ended(p_teletext);
  }
  p_teletext->curr_dispen = is_dispen;

  p_teletext->dispen_pipeline[0] = p_teletext->dispen_pipeline[1];
  p_teletext->dispen_pipeline[1] = p_teletext->incoming_dispen;
}

void
teletext_render(struct teletext_struct* p_teletext,
                struct render_character_1MHz* p_out,
                struct render_character_1MHz* p_next_out) {
  uint32_t i;
  uint32_t src_data_scanline;
  int do_render_rounded_scanline;
  uint8_t* p_src_data = p_teletext->p_render_character;
  uint32_t render_fg_color = p_teletext->render_fg_color;
  uint32_t bg_color = p_teletext->bg_color;
  struct render_character_1MHz* p_black =
      &p_teletext->render_character_1MHz_black;

  if (!p_teletext->curr_dispen) {
    if (p_out) {
      *p_out = *p_black;
    }
    if (p_next_out) {
      *p_next_out = *p_black;
    }
    return;
  }

  if ((p_teletext->flash_active && !p_teletext->flash_visible_this_frame) ||
      (p_teletext->second_character_row_of_double &&
       !p_teletext->double_active)) {
    /* Re-route to space. */
    p_src_data = &s_teletext_generated_glyphs[0];
  }

  src_data_scanline = p_teletext->scanline;
  if (!p_teletext->double_active) {
    src_data_scanline *= 2;
  } else {
    if (p_teletext->second_character_row_of_double) {
      src_data_scanline += 10;
    }
  }

  /* Handle non-interlaced teletext (the glyphs end up weird because character
   * rounded scanline rows aren't selected correctly).
   */
  do_render_rounded_scanline = 0;
  if (!p_teletext->is_isv &&
      p_teletext->crtc_ra0 &&
      !p_teletext->double_active) {
    do_render_rounded_scanline = 1;
  }

  /* Handle interlaced rendering. This is a non-default configuration where
   * the renderer only rasters every other line in the canvas.
   */
  if ((p_next_out == NULL) &&
      p_teletext->crtc_ra0 &&
      !p_teletext->double_active) {
    do_render_rounded_scanline = 1;
  }

  if (do_render_rounded_scanline) {
    src_data_scanline++;
  }

  assert(src_data_scanline < 20);
  p_src_data += (src_data_scanline * 16);

  for (i = 0; i < 16; ++i) {
    uint32_t color;
    uint8_t val = p_src_data[i];

    color = (val * render_fg_color);
    color += ((255 - val) * bg_color);

    p_out->host_pixels[i] = (color | 0xff000000);
  }

  p_out = p_next_out;
  if (p_out == NULL) {
    return;
  }

  /* Another condition to handle non-interlaced teletext. */
  if (p_teletext->is_isv && !p_teletext->double_active) {
    p_src_data += 16;
  }

  for (i = 0; i < 16; ++i) {
    uint32_t color;
    uint8_t val = p_src_data[i];

    color = (val * render_fg_color);
    color += ((255 - val) * bg_color);

    p_out->host_pixels[i] = (color | 0xff000000);
  }
}

void
teletext_RA_ISV_changed(struct teletext_struct* p_teletext,
                        uint8_t ra,
                        int is_isv) {
  p_teletext->crtc_ra0 = (ra & 1);
  p_teletext->is_isv = is_isv;
}

void
teletext_DISPEN_changed(struct teletext_struct* p_teletext, int value) {
  /* The DISPEN signal is pipelined, just like the data bytes are pipelined.
   * The affects of a changing DISPEN are committed in teletext_data() as they
   * hit the head of the pipeline.
   */
  p_teletext->incoming_dispen = value;
}

void
teletext_VSYNC_changed(struct teletext_struct* p_teletext, int value) {
  /* TODO: we've currently only wired this up to VSYNC lower but it will
   * suffice.
   */
  (void) value;

  teletext_new_frame_started(p_teletext);
}
