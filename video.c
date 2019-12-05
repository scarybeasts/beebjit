#include "video.h"

#include "bbc_options.h"
#include "render.h"
#include "teletext.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A real BBC in a non-interlaced bitmapped mode has a CRTC period of 19968us,
 * just short of 20ms.
 * e.g. *TV0,1 (interlace off, defaults to on for the model B)
 *      MODE 0
 * The calculation, with horizontal total 128, vertical total 39, lines
 * per character 8, 2Mhz clock, is:
 * (128 * 39 * 8) / 2
 * For interlaced modes, there's an extra half scanline per frame for a total
 * of exactly 20000us.
 */
static const uint32_t k_video_us_per_vsync = 19968; /* about 20ms / 50Hz */

enum {
  k_crtc_num_registers = 18,
};

static const uint32_t k_crtc_register_mask = 0x1F;

enum {
  k_ula_addr_control = 0,
  k_ula_addr_palette = 1,
};

enum {
  k_ula_teletext = 0x02,
  k_ula_clock_speed = 0x10,

  k_ula_chars_per_line = 0x0C,
  k_ula_chars_per_line_shift = 2,
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
  k_crtc_reg_vert_sync_position = 7,
  k_crtc_reg_interlace = 8,
  k_crtc_reg_lines_per_character = 9,
  k_crtc_reg_cursor_start = 10,
  k_crtc_reg_cursor_end = 11,
  k_crtc_reg_mem_addr_high = 12,
  k_crtc_reg_mem_addr_low = 13,
  k_crtc_reg_cursor_high = 14,
  k_crtc_reg_cursor_low = 15,
  k_crtc_reg_light_pen_high = 16,
  k_crtc_reg_light_pen_low = 17,
};

struct video_struct {
  uint8_t* p_bbc_mem;
  int externally_clocked;
  struct timing_struct* p_timing;
  struct render_struct* p_render;
  void (*func_render_data)(struct render_struct*, uint8_t);
  void (*func_render_blank)(struct render_struct*, uint8_t);
  struct teletext_struct* p_teletext;
  struct via_struct* p_system_via;
  size_t video_timer_id;
  void (*p_framebuffer_ready_callback)();
  void* p_framebuffer_ready_object;

  /* Options. */
  uint32_t frames_skip;
  uint32_t frame_skip_counter;

  /* Timing. */
  uint64_t wall_time;
  uint64_t vsync_next_time;
  uint64_t prev_system_ticks;

  /* Video ULA state. */
  uint8_t video_ula_control;
  uint8_t video_palette[16];
  uint32_t screen_wrap_add;

  /* 6845 registers. */
  uint8_t crtc_address_register;
  uint8_t crtc_registers[k_crtc_num_registers];

  /* 6845 state. */
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint8_t vert_adjust_counter;
  uint8_t vsync_scanline_counter;
  uint32_t address_counter;
  uint32_t address_counter_this_row;
  uint32_t address_counter_next_row;
  int in_vert_adjust;
  int in_vsync;
  int had_vsync_this_frame;
  int display_enable_horiz;
  int display_enable_vert;

  size_t prev_horiz_chars;
  size_t prev_vert_chars;
  int prev_horiz_chars_offset;
  int prev_vert_lines_offset;

  uint8_t video_memory_contiguous[2048];
};

static inline uint32_t
video_calculate_bbc_address(uint32_t* p_out_screen_address,
                            uint32_t address_counter,
                            uint8_t scanline_counter,
                            uint32_t screen_wrap_add) {
  uint32_t address;
  uint32_t screen_address;

  if (address_counter & 0x2000) {
    address = (address_counter & 0x3FF);
    /* MA13 set => MODE7 style addressing. */
    if (address_counter & 0x800) {
      /* Normal MODE7. */
      screen_address = 0x7C00;
    } else {
      /* Unusual quirk -- model B only, not Master. */
      screen_address = 0x3C00;
    }
    address |= screen_address;
  } else {
    /* Normal bitmapped mode. */
    address = (address_counter * 8);
    address += (scanline_counter & 0x7);
    screen_address = (0x8000 - screen_wrap_add);
    if (address_counter & 0x1000) {
      /* MA12 set => screen address wrap around. */
      address -= screen_wrap_add;
    }
    address &= 0x7FFF;
  }

  if (p_out_screen_address != NULL) {
    *p_out_screen_address = screen_address;
  }
  return address;
}

static inline int
video_is_display_enabled(struct video_struct* p_video) {
  uint8_t r8 = p_video->crtc_registers[k_crtc_reg_interlace];
  int r8_enabled = ((r8 & 0x30) != 0x30);
  int r8_video = ((r8 & 0x3) == 0x3);
  int scanline_enabled = (r8_video || !(p_video->scanline_counter & 0x8));
  int enabled = p_video->display_enable_horiz;
  enabled &= p_video->display_enable_vert;
  /* TODO: cache values that are derivative from registers. */
  enabled &= r8_enabled;
  enabled &= scanline_enabled;

  return enabled;
}

static inline void
video_start_new_frame(struct video_struct* p_video) {
  uint32_t address_counter;

  p_video->vert_counter = 0;
  p_video->display_enable_horiz = 1;
  p_video->display_enable_vert = 1;
  p_video->had_vsync_this_frame = 0;
  address_counter = (p_video->crtc_registers[k_crtc_reg_mem_addr_high] << 8);
  address_counter |= p_video->crtc_registers[k_crtc_reg_mem_addr_low];
  p_video->address_counter = address_counter;
  p_video->address_counter_this_row = address_counter;
  /* NOTE: it's untested what happens if you start a new frame, then start a
   * new character row without ever having hit R1 (horizontal displayed).
   */
  p_video->address_counter_next_row = address_counter;
  p_video->in_vert_adjust = 0;
}

static inline void*
video_get_render_function(struct video_struct* p_video) {
  if (video_is_display_enabled(p_video)) {
    return p_video->func_render_data;
  } else {
    return p_video->func_render_blank;
  }
}

static void
video_advance_crtc_timing(struct video_struct* p_video) {
  struct render_struct* p_render = p_video->p_render;
  uint8_t* p_bbc_mem = p_video->p_bbc_mem;
  uint64_t curr_system_ticks =
      timing_get_scaled_total_timer_ticks(p_video->p_timing);
  uint64_t delta_system_ticks = (curr_system_ticks -
                                 p_video->prev_system_ticks);
  uint64_t delta_crtc_ticks = delta_system_ticks;

  uint32_t r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  uint32_t r1 = p_video->crtc_registers[k_crtc_reg_horiz_displayed];
  uint32_t r2 = p_video->crtc_registers[k_crtc_reg_horiz_position];
  uint32_t r4 = p_video->crtc_registers[k_crtc_reg_vert_total];
  uint32_t r6 = p_video->crtc_registers[k_crtc_reg_vert_displayed];
  uint32_t r7 = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  uint32_t r9 = p_video->crtc_registers[k_crtc_reg_lines_per_character];

  void (*func_render_data)(struct render_struct*, uint8_t) =
      render_get_render_data_function(p_render);
  void (*func_render_blank)(struct render_struct*, uint8_t) =
      render_get_render_blank_function(p_render);
  p_video->func_render_data = func_render_data;
  p_video->func_render_blank = func_render_blank;
  void (*func_render)(struct render_struct*, uint8_t) =
      video_get_render_function(p_video);

  uint8_t scanline_stride;
  uint32_t bbc_address;
  uint8_t data;

  int r0_hit;
  int r1_hit;
  int r2_hit;
  int r4_hit;
  int r6_hit;
  int r7_hit;
  int r9_hit;


  if ((p_video->crtc_registers[k_crtc_reg_interlace] & 0x3) == 0x3) {
    scanline_stride = 2;
  } else {
    scanline_stride = 1;
  }

  if (!(p_video->video_ula_control & k_ula_clock_speed)) {
    /* 1MHz mode => CRTC ticks pass at half rate. */
    delta_crtc_ticks /= 2;
  }

  while (delta_crtc_ticks--) {
    uint8_t horiz_counter = p_video->horiz_counter;
    r0_hit = (horiz_counter == r0);
    r1_hit = (horiz_counter == r1);
    r2_hit = (horiz_counter == r2);

    /* Wraps 0xFF -> 0; uint8_t type. */
    p_video->horiz_counter++;
    /* TODO: optimize this to advance by a stride and only recalculate when
     * necessary.
     */
    bbc_address = video_calculate_bbc_address(NULL,
                                              p_video->address_counter,
                                              p_video->scanline_counter,
                                              p_video->screen_wrap_add);

    if (r1_hit) {
      p_video->display_enable_horiz = 0;
      func_render = func_render_blank;
      p_video->address_counter_next_row = p_video->address_counter;
    }
    if (r2_hit) {
      render_hsync(p_render);
    }

    p_video->address_counter = ((p_video->address_counter + 1) & 0x3FFF);

    if (!r0_hit) {
      data = p_bbc_mem[bbc_address];
      func_render(p_render, data);
      continue;
    }

    /* End of horizontal line.
     * There's no display output for this last character.
     */
    func_render_blank(p_render, 0);
    p_video->horiz_counter = 0;

    if (p_video->in_vsync) {
      p_video->vsync_scanline_counter--;
      if (p_video->vsync_scanline_counter == 0) {
        p_video->in_vsync = 0;
        via_set_CA1(p_video->p_system_via, 0);
      }
    }

    r9_hit = (p_video->scanline_counter == r9);
    p_video->scanline_counter = ((p_video->scanline_counter + scanline_stride) &
                                 0x1F);
    p_video->address_counter = p_video->address_counter_this_row;

    if (!r9_hit) {
      /* Incrementing scanline can turn off (or even on) display. For example,
       * MODE3 scanlines 8 & 9.
       */
      func_render = video_get_render_function(p_video);
      continue;
    }

    /* End of character row. */
    p_video->scanline_counter = 0;
    p_video->address_counter = p_video->address_counter_next_row;

    r4_hit = (p_video->vert_counter == r4);
    p_video->vert_counter = ((p_video->vert_counter + 1) & 0x7F);
    r6_hit = (p_video->vert_counter == r6);
    r7_hit = (p_video->vert_counter == r7);

    if (r6_hit) {
      p_video->display_enable_vert = 0;
    }
    if (r7_hit && !p_video->had_vsync_this_frame) {
      p_video->in_vsync = 1;
      p_video->had_vsync_this_frame = 1;
      p_video->vsync_scanline_counter =
          (p_video->crtc_registers[k_crtc_reg_sync_width] >> 4);
      if (p_video->vsync_scanline_counter == 0) {
        p_video->vsync_scanline_counter = 16;
      }
      via_set_CA1(p_video->p_system_via, 1);
      render_vsync(p_render);
    }

    if (!r4_hit) {
      func_render = video_get_render_function(p_video);
      continue;
    }

    video_start_new_frame(p_video);
    func_render = video_get_render_function(p_video);
  }

  p_video->prev_system_ticks = curr_system_ticks;
}

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
  struct timing_struct* p_timing;
  size_t timer_id;

  if (p_video->externally_clocked) {
    return;
  }

  p_timing = p_video->p_timing;
  timer_id = p_video->video_timer_id;
  /* TODO: need to calculate accurate vsync timings here. */
  /* 1024 2Mhz ticks is one character row (8 lines of pixels). */
  (void) timing_set_timer_value(p_timing, timer_id, 1024);
}

static void
video_timer_fired(void* p) {
  struct video_struct* p_video = (struct video_struct*) p;

  assert(timing_get_timer_value(p_video->p_timing, p_video->video_timer_id) ==
         0);
  assert(!p_video->externally_clocked);

  video_advance_crtc_timing(p_video);

  video_update_timer(p_video);
}

struct video_struct*
video_create(uint8_t* p_bbc_mem,
             int externally_clocked,
             struct timing_struct* p_timing,
             struct render_struct* p_render,
             struct teletext_struct* p_teletext,
             struct via_struct* p_system_via,
             void (*p_framebuffer_ready_callback)(void* p),
             void* p_framebuffer_ready_object,
             struct bbc_options* p_options) {
  struct video_struct* p_video = malloc(sizeof(struct video_struct));
  if (p_video == NULL) {
    errx(1, "cannot allocate video_struct");
  }

  (void) memset(p_video, '\0', sizeof(struct video_struct));

  p_video->p_bbc_mem = p_bbc_mem;
  p_video->externally_clocked = externally_clocked;
  p_video->p_timing = p_timing;
  p_video->p_render = p_render;
  p_video->p_teletext = p_teletext;
  p_video->p_system_via = p_system_via;
  p_video->p_framebuffer_ready_callback = p_framebuffer_ready_callback;
  p_video->p_framebuffer_ready_object = p_framebuffer_ready_object;

  p_video->wall_time = 0;
  p_video->vsync_next_time = 0;

  p_video->video_timer_id = timing_register_timer(p_timing,
                                                  video_timer_fired,
                                                  p_video);

  p_video->crtc_address_register = 0;

  p_video->frames_skip = 0;
  p_video->frame_skip_counter = 0;
  (void) util_get_u32_option(&p_video->frames_skip,
                             p_options->p_opt_flags,
                             "video:frames-skip=");

  /* What initial state should we use for 6845 and Video ULA registers?
   * The 6845 data sheets (all variations?) aren't much help, quoting:
   * http://bitsavers.trailing-edge.com/components/motorola/_dataSheets/6845.pdf
   * "The CRTC registers will have an initial value at power up. When using
   * a direct drive monitor (sans horizontal oscillator) these initial values
   * may result in out-of-tolerance operation."
   * It isn't specified whether those initial values are random or
   * deterministic, or what they may be.
   * Custom MOS ROM tests by Tom Seddon indicate fairly random values from boot
   * to boot; sometimes the register values result in VSYNCs, sometimes not.
   * We could argue it's not a huge deal because the MOS ROM sets values for
   * the registers as part of selecting MODE7 at boot up. But we do want to
   * avoid exotic 6845 setups (such as registers all zero) because the timing
   * of exotic setups is more likely to change as bugs are fixed. And stable
   * timing is desirable on account of record / playback support.
   * So TL;DR: we'll set up MODE7.
   */
  p_video->crtc_registers[k_crtc_reg_horiz_total] = 63;
  p_video->crtc_registers[k_crtc_reg_horiz_displayed] = 40;
  p_video->crtc_registers[k_crtc_reg_horiz_position] = 51;
  /* Horiz sync pulse width 4, vertical sync pulse width 2. */
  p_video->crtc_registers[k_crtc_reg_sync_width] = (4 | (2 << 4));
  p_video->crtc_registers[k_crtc_reg_vert_total] = 30;
  p_video->crtc_registers[k_crtc_reg_vert_adjust] = 2;
  p_video->crtc_registers[k_crtc_reg_vert_displayed] = 25;
  p_video->crtc_registers[k_crtc_reg_vert_sync_position] = 27;
  /* Interlace sync and video, 1 character display delay, 2 character cursor
   * delay.
   */
  p_video->crtc_registers[k_crtc_reg_interlace] = (3 | (1 << 4) | (2 << 6));
  p_video->crtc_registers[k_crtc_reg_lines_per_character] = 18;

  /* Teletext mode, 1MHz operation. */
  p_video->video_ula_control = 2;

  video_init_timer(p_video);
  video_start_new_frame(p_video);
  video_update_timer(p_video);

  return p_video;
}

void
video_destroy(struct video_struct* p_video) {
  free(p_video);
}

void
video_IC32_updated(struct video_struct* p_video, uint8_t IC32) {
  uint32_t screen_wrap_add;

  uint32_t size_id = ((IC32 >> 4) & 0x3);

  /* Note: doesn't seem to match the BBC Microcomputer Advanced User Guide, but
   * does work and does match b-em.
   */
  switch (size_id) {
  case 0:
    screen_wrap_add = 0x4000;
    break;
  case 1:
    screen_wrap_add = 0x2000;
    break;
  case 2:
    screen_wrap_add = 0x5000;
    break;
  case 3:
    screen_wrap_add = 0x2800;
    break;
  default:
    assert(0);
    break;
  }

  if (p_video->screen_wrap_add == screen_wrap_add) {
    return;
  }

  /* Changing the screen wrap addition could affect rendering, so catch up. */
  if (!p_video->externally_clocked) {
    video_advance_crtc_timing(p_video);
  }

  p_video->screen_wrap_add = screen_wrap_add;
}

struct render_struct*
video_get_render(struct video_struct* p_video) {
  return p_video->p_render;
}

void
video_apply_wall_time_delta(struct video_struct* p_video, uint64_t delta) {
  uint64_t wall_time;

  wall_time = (p_video->wall_time + delta);
  p_video->wall_time = wall_time;

  if (wall_time < p_video->vsync_next_time) {
    return;
  }
  while (p_video->vsync_next_time <= wall_time) {
    p_video->vsync_next_time += k_video_us_per_vsync;
  }

  if (p_video->externally_clocked) {
    struct via_struct* p_system_via = p_video->p_system_via;
    via_set_CA1(p_system_via, 0);
    via_set_CA1(p_system_via, 1);
  }

  if (p_video->frame_skip_counter == 0) {
    p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object);
    p_video->frame_skip_counter = p_video->frames_skip;
  } else {
    p_video->frame_skip_counter--;
  }
}

static void
video_1MHz_mode_render(struct video_struct* p_video,
                       int mode,
                       size_t horiz_chars,
                       size_t vert_chars,
                       size_t horiz_chars_offset,
                       size_t vert_lines_offset) {
  size_t y;
  struct render_struct* p_render = video_get_render(p_video);
  uint32_t* p_frame_buf = render_get_buffer(p_render);
  uint32_t width = render_get_width(p_render);
  struct render_table_1MHz* p_render_table =
      render_get_1MHz_render_table(p_render, mode);
  uint32_t video_memory_row_size = (horiz_chars * 8);
  uint32_t offset = 0;

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    uint8_t* p_video_mem = video_get_video_memory_slice(p_video,
                                                        offset,
                                                        video_memory_row_size);
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t data = *p_video_mem++;
        uint32_t* p_render_buffer = (uint32_t*) p_frame_buf;
        struct render_character_1MHz* p_character_buffer;
        p_render_buffer += (((y * 8) + y2 + vert_lines_offset) * 2 * width);
        p_render_buffer += ((x + horiz_chars_offset) * 16);
        p_character_buffer = (struct render_character_1MHz*) p_render_buffer;
        *p_character_buffer = p_render_table->values[data];
      }
    }

    offset += horiz_chars;;
  }
}

static void
video_2MHz_mode_render(struct video_struct* p_video,
                       int mode,
                       size_t horiz_chars,
                       size_t vert_chars,
                       size_t horiz_chars_offset,
                       size_t vert_lines_offset) {
  size_t y;

  struct render_struct* p_render = video_get_render(p_video);
  uint32_t* p_frame_buf = render_get_buffer(p_render);
  uint32_t width = render_get_width(p_render);
  struct render_table_2MHz* p_render_table =
      render_get_2MHz_render_table(p_render, mode);
  uint32_t video_memory_row_size = (horiz_chars * 8);
  uint32_t offset = 0;

  for (y = 0; y < vert_chars; ++y) {
    size_t x;
    uint8_t* p_video_mem = video_get_video_memory_slice(p_video,
                                                        offset,
                                                        video_memory_row_size);
    for (x = 0; x < horiz_chars; ++x) {
      size_t y2;
      for (y2 = 0; y2 < 8; ++y2) {
        uint8_t data = *p_video_mem++;
        uint32_t* p_render_buffer = (uint32_t*) p_frame_buf;
        struct render_character_2MHz* p_character_buffer;
        p_render_buffer += (((y * 8) + y2 + vert_lines_offset) * 2 * width);
        p_render_buffer += ((x + horiz_chars_offset) * 8);
        p_character_buffer = (struct render_character_2MHz*) p_render_buffer;
        *p_character_buffer = p_render_table->values[data];
      }
    }

    offset += horiz_chars;
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
  uint8_t clock_speed = !!(ula_control & k_ula_clock_speed);
  return clock_speed;
}

void
video_render_full_frame(struct video_struct* p_video) {
  int is_text;
  size_t pixel_width;
  size_t clock_speed;
  size_t horiz_chars;
  size_t vert_chars;
  size_t vert_lines;
  int horiz_chars_offset;
  int vert_lines_offset;

  size_t max_horiz_chars;
  size_t max_vert_lines;

  struct render_struct* p_render = p_video->p_render;
  uint32_t* p_render_buffer = render_get_buffer(p_render);
  if (p_render_buffer == NULL) {
    return;
  }

  is_text = video_is_text(p_video);
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
    render_clear_buffer(p_render);
  }
  p_video->prev_horiz_chars = horiz_chars;
  p_video->prev_vert_chars = vert_chars;
  p_video->prev_horiz_chars_offset = horiz_chars_offset;
  p_video->prev_vert_lines_offset = vert_lines_offset;

  if (is_text) {
    /* NOTE: what happens with crazy combinations, such as 2MHz clock here? */
    struct teletext_struct* p_teletext = p_video->p_teletext;
    teletext_render_full(p_teletext, p_video);
  } else if (clock_speed == 1) {
    int mode = k_render_mode0;
    switch (pixel_width) {
    case 2:
      mode = k_render_mode1;
      break;
    case 4:
      mode = k_render_mode2;
      break;
    case 1:
    default:
      break;
    }
    video_2MHz_mode_render(p_video,
                           mode,
                           horiz_chars,
                           vert_chars,
                           horiz_chars_offset,
                           vert_lines_offset);
  } else {
    int mode = k_render_mode4;
    assert(clock_speed == 0);
    switch (pixel_width) {
    case 4:
      mode = k_render_mode5;
      break;
    case 1:
    default:
      break;
    }
    video_1MHz_mode_render(p_video,
                           mode,
                           horiz_chars,
                           vert_chars,
                           horiz_chars_offset,
                           vert_lines_offset);
  }

  render_double_up_lines(p_render);
}

void
video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t index;
  uint8_t rgbf;
  uint32_t color;

  if (addr == 0) {
    /* Writing the control register can affect timing if the 6845 clock rate
     * is changed.
     */
    /* TODO: only need to call this if clock rate changes. MOS changes the
     * "flash colour select" bit at some rate, which affects rendering but
     * not timing.
     */
    if (!p_video->externally_clocked) {
      video_advance_crtc_timing(p_video);
    }

    p_video->video_ula_control = val;

    return;
  }

  assert(addr == 1);

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

  render_set_palette(p_video->p_render, index, color);
}

uint8_t
video_crtc_read(struct video_struct* p_video, uint8_t addr) {
  uint8_t reg;

  assert(addr < 2);

  /* EMU NOTE: read-only registers in CRTC return 0, confirmed on a real BBC
   * here:
   * https://stardot.org.uk/forums/viewtopic.php?f=4&t=17509
   */
  if (addr == 0) {
    /* CRTC latched register is read-only. */
    return 0;
  }

  /* EMU NOTE: Not all 6845s are identical. The BBC uses the Hitachi one:
   * https://www.cpcwiki.eu/imgs/c/c0/Hd6845.hitachi.pdf
   * Of note, the memory address registers are readable, which is not the case
   * for other 6845s.
   */
  reg = p_video->crtc_address_register;
  switch (reg) {
  case k_crtc_reg_mem_addr_high:
  case k_crtc_reg_mem_addr_low:
  case k_crtc_reg_cursor_high:
  case k_crtc_reg_cursor_low:
  case k_crtc_reg_light_pen_high:
  case k_crtc_reg_light_pen_low:
    return p_video->crtc_registers[reg];
  default:
    break;
  }

  return 0;
}

void
video_crtc_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t hsync_width;
  uint8_t vsync_width;
  uint8_t reg;

  uint8_t mask = 0xFF;

  if (addr == k_crtc_addr_reg) {
    p_video->crtc_address_register = (val & k_crtc_register_mask);
    return;
  }

  assert(addr == k_crtc_addr_val);

  if (!p_video->externally_clocked) {
    video_advance_crtc_timing(p_video);
  }

  reg = p_video->crtc_address_register;

  if (reg >= k_crtc_num_registers) {
    return;
  }
  switch (reg) {
  case k_crtc_reg_light_pen_high:
  case k_crtc_reg_light_pen_low:
    /* Light pen registers are read only. */
    return;
  default:
    break;
  }

  switch (reg) {
  /* R0 */
  case k_crtc_reg_horiz_total:
    if ((val != 63) && (val != 127)) {
      printf("LOG:CRTC:unusual horizontal total: %d\n", val);
    }
    break;
  /* R3 */
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
  /* R4 */
  case k_crtc_reg_vert_total:
    mask = 0x7F;
    if ((val != 38) && (val != 30)) {
      printf("LOG:CRTC:unusual vertical total: %d\n", val);
    }
    break;
  /* R5 */
  case k_crtc_reg_vert_adjust:
    mask = 0x1F;
    break;
  /* R6 */
  case k_crtc_reg_vert_displayed:
    mask = 0x7F;
    break;
  /* R7 */
  case k_crtc_reg_vert_sync_position:
    mask = 0x7F;
    break;
  /* R9 */
  case k_crtc_reg_lines_per_character:
    mask = 0x1F;
    if (val != 7) {
      printf("LOG:CRTC:scan lines per character != 7: %d\n", val);
    }
    break;
  /* R10 */
  case k_crtc_reg_cursor_start:
    mask = 0x7F;
    break;
  /* R11 */
  case k_crtc_reg_cursor_end:
    mask = 0x1F;
    break;
  /* R12 */
  case k_crtc_reg_mem_addr_high:
    mask = 0x3F;
    break;
  /* R14 */
  case k_crtc_reg_cursor_high:
    mask = 0x3F;
    break;
  default:
    break;
  }

  p_video->crtc_registers[reg] = (val & mask);
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
  uint32_t i;
  for (i = 0; i < k_crtc_num_registers; ++i) {
    p_values[i] = p_video->crtc_registers[i];
  }
}

void
video_set_crtc_registers(struct video_struct* p_video,
                         const uint8_t* p_values) {
  uint32_t i;
  for (i = 0; i < k_crtc_num_registers; ++i) {
    p_video->crtc_registers[i] = p_values[i];
  }
}

uint8_t*
video_get_video_memory_slice(struct video_struct* p_video,
                             uint32_t crtc_offset,
                             uint32_t length) {
  uint32_t crtc_address;
  uint32_t screen_address;
  uint32_t bbc_address;
  uint32_t first_chunk_len;
  uint32_t second_chunk_len;

  uint8_t* p_bbc_mem = p_video->p_bbc_mem;

  assert(crtc_offset < 0x1000);
  assert(length < 0x1000);

  if (length > sizeof(p_video->video_memory_contiguous)) {
    errx(1, "excessive length in video_get_video_memory_slice");
  }

  crtc_address = (p_video->crtc_registers[k_crtc_reg_mem_addr_high] << 8);
  crtc_address |= p_video->crtc_registers[k_crtc_reg_mem_addr_low];
  crtc_address += crtc_offset;

  bbc_address = video_calculate_bbc_address(&screen_address,
                                            crtc_address,
                                            0,
                                            p_video->screen_wrap_add);

  /* NOTE: won't work for the weird 0x3C00 MODE7 quirk.
   * Also won't work for crtc addresses that cross a behavior boundary, i.e.
   * 0x3FFF -> 0x0000.
   */
  if ((bbc_address + length) <= 0x8000) {
    return (p_bbc_mem + bbc_address);
  }

  /* This video memory slice wraps around so build a contiguous buffer. */
  first_chunk_len = (0x8000 - bbc_address);
  second_chunk_len = (length - first_chunk_len);
  (void) memcpy(&p_video->video_memory_contiguous[0],
                (p_bbc_mem + bbc_address),
                first_chunk_len);
  (void) memcpy((&p_video->video_memory_contiguous[0] + first_chunk_len),
                (p_bbc_mem + screen_address),
                second_chunk_len);

  return &p_video->video_memory_contiguous[0];
}

size_t
video_get_horiz_chars(struct video_struct* p_video, size_t clock_speed) {
  /* NOTE: clock_speed is passed in rather than fetched, to avoid race
   * conditions where the BBC thread is changing values from under us.
   */
  size_t ret = p_video->crtc_registers[k_crtc_reg_horiz_displayed];

  if (clock_speed == 1 && ret > 80) {
    ret = 80;
  } else if (clock_speed == 0 && ret > 40) {
    ret = 40;
  }

  return ret;
}

size_t
video_get_vert_chars(struct video_struct* p_video) {
  size_t ret = p_video->crtc_registers[k_crtc_reg_vert_displayed];
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
  int ret = p_video->crtc_registers[k_crtc_reg_horiz_position];
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
  uint8_t pos = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  uint8_t adjust = p_video->crtc_registers[k_crtc_reg_vert_adjust];
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
