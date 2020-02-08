#include "video.h"

#include "bbc_options.h"
#include "log.h"
#include "render.h"
#include "teletext.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <err.h>
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
  k_ula_flash = 0x01,
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
  struct teletext_struct* p_teletext;
  struct via_struct* p_system_via;
  size_t video_timer_id;
  void (*p_framebuffer_ready_callback)(void*, int, int);
  void* p_framebuffer_ready_object;
  int* p_fast_flag;

  /* Rendering. */
  struct render_struct* p_render;
  int render_mode;
  int is_framing_changed_for_render;
  int is_wall_time_vsync_hit;
  int is_rendering_active;

  /* Options. */
  uint32_t frames_skip;
  uint32_t frame_skip_counter;
  int render_after_each_row;
  int log_timer;

  /* Timing. */
  uint64_t wall_time;
  uint64_t vsync_next_time;
  uint64_t prev_system_ticks;
  int timer_fire_expect_vsync_start;
  int timer_fire_expect_vsync_end;
  uint64_t num_vsyncs;
  uint64_t num_crtc_advances;

  /* Video ULA state. */
  uint8_t video_ula_control;
  uint8_t ula_palette[16];
  uint32_t screen_wrap_add;

  /* 6845 registers and derivatives. */
  uint8_t crtc_address_register;
  uint8_t crtc_registers[k_crtc_num_registers];
  int is_interlace;
  int is_interlace_sync_and_video;
  int is_master_display_enable;
  uint32_t scanline_stride;
  uint32_t scanline_mask;
  uint8_t hsync_pulse_width;
  uint8_t vsync_pulse_width;
  uint8_t half_r0;

  /* 6845 state. */
  uint64_t crtc_frames;
  int is_even_interlace_frame;
  int is_odd_interlace_frame;
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
  int in_dummy_raster;
  int had_vsync_this_frame;
  int display_enable_horiz;
  int display_enable_vert;
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
  int scanline_enabled = (p_video->is_interlace_sync_and_video ||
                          !(p_video->scanline_counter & 0x8));
  int enabled = p_video->is_master_display_enable;
  enabled &= p_video->display_enable_horiz;
  enabled &= p_video->display_enable_vert;
  enabled &= scanline_enabled;

  return enabled;
}

static inline void
video_start_new_frame(struct video_struct* p_video) {
  uint32_t address_counter;

  p_video->horiz_counter = 0;
  p_video->scanline_counter = 0;
  p_video->vert_counter = 0;
  p_video->vert_adjust_counter = 0;

  p_video->had_vsync_this_frame = 0;
  p_video->in_vert_adjust = 0;
  p_video->in_dummy_raster = 0;

  p_video->display_enable_horiz = 1;
  p_video->display_enable_vert = 1;
  address_counter = (p_video->crtc_registers[k_crtc_reg_mem_addr_high] << 8);
  address_counter |= p_video->crtc_registers[k_crtc_reg_mem_addr_low];
  p_video->address_counter = address_counter;
  p_video->address_counter_this_row = address_counter;
  /* NOTE: it's untested what happens if you start a new frame, then start a
   * new character row without ever having hit R1 (horizontal displayed).
   */
  p_video->address_counter_next_row = address_counter;

  render_frame_boundary(p_video->p_render);
}

static inline int
video_get_clock_speed(struct video_struct* p_video) {
  return !!(p_video->video_ula_control & k_ula_clock_speed);
}

static void
video_do_paint(struct video_struct* p_video) {
  int do_full_render = p_video->externally_clocked;
  p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object,
                                        do_full_render,
                                        p_video->is_framing_changed_for_render);
  p_video->is_framing_changed_for_render = 0;

  if (p_video->externally_clocked) {
    return;
  }

  /* If we're in fast mode and internally clocked, give rendering and painting
   * a rest after each paint.
   * We'll get prodded to start again by the 50Hz real time tick, which will
   * get noticed in video_timer_fired().
   */
  if (*p_video->p_fast_flag) {
    p_video->is_rendering_active = 0;
  }
}

static void
video_set_vsync_raise_state(struct video_struct* p_video) {
  struct via_struct* p_system_via = p_video->p_system_via;

  assert(!p_video->in_vsync);
  assert(!p_video->had_vsync_this_frame);

  p_video->in_vsync = 1;
  p_video->had_vsync_this_frame = 1;
  p_video->vsync_scanline_counter = p_video->vsync_pulse_width;
  if (p_system_via) {
    via_set_CA1(p_system_via, 1);
  }

  if (p_video->is_rendering_active) {
    video_do_paint(p_video);
    render_vsync(p_video->p_render);
  }
}

static void
video_set_vsync_lower_state(struct video_struct* p_video) {
  struct via_struct* p_system_via = p_video->p_system_via;

  assert(p_video->in_vsync);

  p_video->in_vsync = 0;
  if (p_system_via) {
    via_set_CA1(p_system_via, 0);
  }

  teletext_VSYNC_changed(p_video->p_teletext, 0);
}

static inline int
video_is_check_vsync_at_half_r0(struct video_struct* p_video) {
  if (!p_video->is_odd_interlace_frame) {
    return 0;
  }
  if (p_video->in_vsync && (p_video->vsync_scanline_counter == 0)) {
    return 1;
  }
  if ((p_video->vert_counter ==
       p_video->crtc_registers[k_crtc_reg_vert_sync_position]) &&
      (p_video->scanline_counter == 0) &&
      !p_video->had_vsync_this_frame) {
    return 1;
  }
  return 0;
}

static inline void
video_update_odd_even_frame(struct video_struct* p_video) {
  p_video->is_even_interlace_frame = (p_video->is_interlace &&
                                      !(p_video->crtc_frames & 1));
  p_video->is_odd_interlace_frame = (p_video->is_interlace &&
                                     (p_video->crtc_frames & 1));
}

static void
video_advance_crtc_timing(struct video_struct* p_video) {
  uint32_t bbc_address;
  uint8_t data;
  uint64_t delta_crtc_ticks;
  int check_vsync_at_half_r0;

  int r0_hit;
  int r1_hit;
  int r2_hit;
  int r4_hit;
  int r5_hit;
  int r6_hit;
  int r7_hit;
  int r9_hit;

  void (*func_render)(struct render_struct*, uint8_t);

  struct render_struct* p_render = p_video->p_render;
  uint8_t* p_bbc_mem = p_video->p_bbc_mem;
  uint64_t curr_system_ticks =
      timing_get_scaled_total_timer_ticks(p_video->p_timing);
  int clock_speed = video_get_clock_speed(p_video);

  uint32_t r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  uint32_t r1 = p_video->crtc_registers[k_crtc_reg_horiz_displayed];
  uint32_t r2 = p_video->crtc_registers[k_crtc_reg_horiz_position];
  uint32_t r4 = p_video->crtc_registers[k_crtc_reg_vert_total];
  uint32_t r5 = p_video->crtc_registers[k_crtc_reg_vert_adjust];
  uint32_t r6 = p_video->crtc_registers[k_crtc_reg_vert_displayed];
  uint32_t r7 = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  uint32_t r9 = p_video->crtc_registers[k_crtc_reg_lines_per_character];

  void (*func_render_data)(struct render_struct*, uint8_t) =
      render_get_render_data_function(p_render);
  void (*func_render_blank)(struct render_struct*, uint8_t) =
      render_get_render_blank_function(p_render);

  p_video->num_crtc_advances++;

  delta_crtc_ticks = (curr_system_ticks - p_video->prev_system_ticks);

  if (clock_speed == 0) {
    /* Round down if we're advancing to an odd clock. */
    if (curr_system_ticks & 1) {
      delta_crtc_ticks--;
    }
    /* Round up if we're advancing from an odd clock. */
    if (p_video->prev_system_ticks & 1) {
      delta_crtc_ticks++;
    }
    assert(!(delta_crtc_ticks & 1));
    /* 1MHz mode => CRTC ticks pass at half rate. */
    delta_crtc_ticks /= 2;
  }

  goto recalculate_and_continue;

  while (delta_crtc_ticks--) {
    r0_hit = (p_video->horiz_counter == r0);
    r1_hit = (p_video->horiz_counter == r1);
    r2_hit = (p_video->horiz_counter == r2);

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
      if (p_video->display_enable_vert) {
        teletext_DISPMTG_changed(p_video->p_teletext, 0);
      }
    }
    if (r2_hit) {
      render_hsync(p_render);
    }
    if (check_vsync_at_half_r0 &&
        (p_video->horiz_counter == p_video->half_r0)) {
      if (p_video->in_vsync) {
        video_set_vsync_lower_state(p_video);
      } else {
        video_set_vsync_raise_state(p_video);
      }
      check_vsync_at_half_r0 = 0;
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
    p_video->display_enable_horiz = 1;

    if (p_video->in_vsync) {
      if (p_video->vsync_scanline_counter > 0) {
        p_video->vsync_scanline_counter--;
      }
      if ((p_video->vsync_scanline_counter == 0) &&
          !p_video->is_odd_interlace_frame) {
        assert(!check_vsync_at_half_r0);
        video_set_vsync_lower_state(p_video);
      }
    }

    r9_hit = (p_video->scanline_counter == (r9 & p_video->scanline_mask));
    p_video->scanline_counter += p_video->scanline_stride;
    p_video->scanline_counter &= p_video->scanline_mask;
    p_video->address_counter = p_video->address_counter_this_row;

    if (p_video->in_dummy_raster) {
      goto start_new_frame;
    }

    if (p_video->in_vert_adjust) {
      p_video->vert_adjust_counter++;
      p_video->vert_adjust_counter &= 0x1F;
      r5_hit = (p_video->vert_adjust_counter == r5);
      if (r5_hit) {
        /* The dummy raster is at the end of the even frame; the test for odd
         * here is because the R6 hit earlier in a "normal" frame will have
         * incremented the CRTC frame count.
         * See http://bitsavers.trailing-edge.com/components/motorola/_dataSheets/6845.pdf
         * for details about the dummy raster and interlace timing.
         */
        if (p_video->is_odd_interlace_frame) {
          p_video->in_vert_adjust = 0;
          p_video->in_dummy_raster = 1;
        } else {
          goto start_new_frame;
        }
      }
      goto recalculate_and_continue;
    }

    if (!r9_hit) {
      goto recalculate_and_continue;
    }

    /* End of character row. */
    p_video->scanline_counter = 0;
    p_video->address_counter = p_video->address_counter_next_row;
    p_video->address_counter_this_row = p_video->address_counter_next_row;

    r4_hit = (p_video->vert_counter == r4);
    p_video->vert_counter = ((p_video->vert_counter + 1) & 0x7F);

    if (r4_hit) {
      /* End of R4-based frame. Time for either vertical adjust, dummy raster or
       * new frame.
       */
      if (r5 != 0) {
        p_video->in_vert_adjust = 1;
      } else if (p_video->is_odd_interlace_frame) {
        p_video->in_dummy_raster = 1;
      } else {
start_new_frame:
        video_start_new_frame(p_video);
      }
    }

    r6_hit = (p_video->vert_counter == r6);
    r7_hit = (p_video->vert_counter == r7);

    if (r6_hit) {
      p_video->display_enable_vert = 0;
      /* On the Hitachi 6845, frame counting is done on R6 hit. */
      p_video->crtc_frames++;
      video_update_odd_even_frame(p_video);
    }
    if (r7_hit &&
        !p_video->is_odd_interlace_frame &&
        !p_video->had_vsync_this_frame) {
      assert(!check_vsync_at_half_r0);

      video_set_vsync_raise_state(p_video);
    }

recalculate_and_continue:
    func_render = video_is_display_enabled(p_video) ?
        func_render_data : func_render_blank;
    check_vsync_at_half_r0 = video_is_check_vsync_at_half_r0(p_video);
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

static inline int
video_get_flash(struct video_struct* p_video) {
  return !!(p_video->video_ula_control & k_ula_flash);
}

static void
video_update_timer(struct video_struct* p_video) {
  struct timing_struct* p_timing;
  uint32_t timer_id;
  uint64_t timer_value;

  uint32_t r0;
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r9;

  uint32_t tick_multiplier;
  uint32_t scanline_ticks;
  uint32_t half_scanline_ticks;
  uint32_t ticks_to_next_scanline;
  uint32_t ticks_to_next_row;
  uint32_t scanlines_per_row;
  uint32_t scanlines_left_this_row;
  uint32_t vert_counter_max;

  int clock_speed = video_get_clock_speed(p_video);

  if (p_video->externally_clocked) {
    return;
  }

  p_video->timer_fire_expect_vsync_start = 0;
  p_video->timer_fire_expect_vsync_end = 0;

  p_timing = p_video->p_timing;
  timer_id = p_video->video_timer_id;

  r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  r4 = p_video->crtc_registers[k_crtc_reg_vert_total];
  r5 = p_video->crtc_registers[k_crtc_reg_vert_adjust];
  r6 = p_video->crtc_registers[k_crtc_reg_vert_displayed];
  r7 = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  r9 = p_video->crtc_registers[k_crtc_reg_lines_per_character];

  vert_counter_max = r4;
  if (r5 > 0) {
    vert_counter_max++;
  }

  tick_multiplier = 1;
  if (clock_speed == 0) {
    tick_multiplier = 2;
  }
  scanline_ticks = (r0 + 1);

  if (p_video->horiz_counter <= r0) {
    ticks_to_next_scanline = ((r0 - p_video->horiz_counter) + 1);
  } else {
    ticks_to_next_scanline = (0x100 - p_video->horiz_counter);
    ticks_to_next_scanline += (r0 + 1);
  }

  /* NOTE: one scanline has already been taken care of via the "ticks to next
   * scanline" calculation above. So, this is additional scanlines once the
   * current one has finished.
   */
  if (p_video->in_dummy_raster) {
    scanlines_left_this_row = 0;
  } else if (p_video->in_vert_adjust) {
    if (p_video->vert_adjust_counter < r5) {
      scanlines_left_this_row = (r5 - p_video->vert_adjust_counter);
      scanlines_left_this_row--;
    } else {
      scanlines_left_this_row = (0x1F - p_video->vert_adjust_counter);
      scanlines_left_this_row += r5;
    }
  } else {
    if (p_video->scanline_counter <= r9) {
      scanlines_left_this_row = (r9 - p_video->scanline_counter);
    } else {
      scanlines_left_this_row = (0x1F - p_video->scanline_counter);
      scanlines_left_this_row += (r9 + 1);
    }
    scanlines_left_this_row /= p_video->scanline_stride;
  }

  scanlines_per_row = (r9 / p_video->scanline_stride);
  scanlines_per_row++;

  scanline_ticks *= tick_multiplier;
  half_scanline_ticks = (scanline_ticks / 2);
  ticks_to_next_scanline *= tick_multiplier;
  ticks_to_next_row = ticks_to_next_scanline;
  ticks_to_next_row += (scanline_ticks * scanlines_left_this_row);

  if (p_video->log_timer) {
    log_do_log(k_log_video,
               k_log_info,
               "hc %d sc %d ac %d vc %d "
               "r4 %d r5 %d r6 %d r7 %d r9 %d "
               "adj %d i %d v %d m %d t %zu",
               p_video->horiz_counter,
               p_video->scanline_counter,
               p_video->vert_adjust_counter,
               p_video->vert_counter,
               r4,
               r5,
               r6,
               r7,
               r9,
               p_video->in_vert_adjust,
               (p_video->crtc_registers[k_crtc_reg_interlace] & 3),
               p_video->had_vsync_this_frame,
               tick_multiplier,
               timing_get_total_timer_ticks(p_video->p_timing));
  }

  if (p_video->in_vsync) {
    if (!p_video->is_odd_interlace_frame) {
      assert(p_video->vsync_scanline_counter > 0);
      timer_value = ticks_to_next_scanline;
      timer_value += ((p_video->vsync_scanline_counter - 1) * scanline_ticks);
    } else {
      uint32_t full_scanlines = p_video->vsync_scanline_counter;
      if (p_video->horiz_counter <= p_video->half_r0) {
        timer_value = ((p_video->half_r0 - p_video->horiz_counter) *
                       tick_multiplier);
      } else {
        timer_value = ticks_to_next_scanline;
        timer_value += (p_video->half_r0 * tick_multiplier);
        if (full_scanlines > 0) {
          --full_scanlines;
        }
      }
      timer_value += (full_scanlines * scanline_ticks);
    }
    p_video->timer_fire_expect_vsync_end = 1;
  } else if ((p_video->vert_counter < r7) &&
             (r7 <= vert_counter_max) &&
             !p_video->had_vsync_this_frame) {
    /* In this branch, vsync will happen this current frame. */
    assert(!p_video->in_vert_adjust);
    timer_value = ticks_to_next_row;
    timer_value += (scanline_ticks *
                    scanlines_per_row *
                    (r7 - p_video->vert_counter - 1));
    if (p_video->is_even_interlace_frame && (p_video->vert_counter < r6)) {
      timer_value += half_scanline_ticks;
    } else if (p_video->is_odd_interlace_frame &&
               (p_video->vert_counter >= r6)) {
      timer_value += half_scanline_ticks;
    }
    p_video->timer_fire_expect_vsync_start = 1;
  } else if ((p_video->vert_counter == r7) &&
             p_video->is_odd_interlace_frame &&
             (p_video->scanline_counter == 0) &&
             (p_video->horiz_counter < p_video->half_r0) &&
             !p_video->had_vsync_this_frame) {
    /* Special case for interlace mode where vsync can fire shortly after R7
     * is hit, in the even field.
     */
    timer_value = (p_video->half_r0 - p_video->horiz_counter);
    timer_value *= tick_multiplier;
    p_video->timer_fire_expect_vsync_start = 1;
  } else if (r7 > r4) {
    /* If vsync'ing isn't happening, just wake up every 50Hz or so. */
    timer_value = (k_video_us_per_vsync * 2);
  } else {
    /* Vertical counter is already past the vsync position, so we need to
     * calculate the timing to the end of the frame (or vertical counter wrap),
     * plus the timing from start of frame to vsync.
     */
    uint32_t ticks_to_end_of_frame;
    uint32_t ticks_from_frame_to_vsync;

    int is_even_interlace_frame = 0;
    int is_wrap = 0;

    if (p_video->is_interlace) {
      is_even_interlace_frame = p_video->is_even_interlace_frame;
      if ((p_video->vert_counter < r6) &&
          !p_video->in_vert_adjust &&
          !p_video->in_dummy_raster) {
        is_even_interlace_frame = !is_even_interlace_frame;
      }
    }

    ticks_from_frame_to_vsync = (scanline_ticks * scanlines_per_row * r7);
    if (p_video->is_interlace && is_even_interlace_frame) {
      ticks_from_frame_to_vsync += half_scanline_ticks;
    }

    ticks_to_end_of_frame = ticks_to_next_row;
    if (!p_video->in_vert_adjust && !p_video->in_dummy_raster) {
      uint32_t rows_to_end_of_frame;

      if (p_video->vert_counter <= r4) {
        rows_to_end_of_frame = (r4 - p_video->vert_counter);
        ticks_to_end_of_frame += (scanline_ticks * r5);
      } else {
        rows_to_end_of_frame = (0x7F - p_video->vert_counter);
        is_wrap = 1;
      }
      ticks_to_end_of_frame += (scanline_ticks *
                                scanlines_per_row *
                                rows_to_end_of_frame);
    }
    if (!p_video->in_dummy_raster &&
        p_video->is_interlace &&
        !is_even_interlace_frame &&
        !is_wrap) {
      /* Add in the dummy raster. */
      ticks_to_end_of_frame += scanline_ticks;
    }

    timer_value = (ticks_to_end_of_frame + ticks_from_frame_to_vsync);
    p_video->timer_fire_expect_vsync_start = 1;
  }

  assert(timer_value != 0);

  if ((clock_speed == 0) &&
      (timing_get_scaled_total_timer_ticks(p_timing) & 1)) {
    /* If we switched to 1MHz on an odd cycle, the first CRTC tick will only
     * take half the time.
     */
    timer_value--;
  }

  assert(timer_value < (4 * 1024 * 1024));

  if (p_video->log_timer) {
    log_do_log(k_log_video, k_log_info, "timer value: %zu", timer_value);
  }

  /* beebjit does not synchronize the video RAM read with the CPU RAM writes.
   * This leads to juddery display in same games where screen RAM updates are
   * carefully arranged at some useful point relative to vsync.
   * Fortess is a good example.
   * This option here renders after each character row, leading to a more
   * accurate display at the cost of some performance.
   */
  if (p_video->render_after_each_row &&
      p_video->is_rendering_active &&
      (timer_value > ticks_to_next_row)) {
    timer_value = ticks_to_next_row;
    p_video->timer_fire_expect_vsync_start = 0;
    p_video->timer_fire_expect_vsync_end = 0;
  }

  (void) timing_set_timer_value(p_timing, timer_id, timer_value);
}

static void
video_timer_fired(void* p) {
  struct video_struct* p_video = (struct video_struct*) p;

  assert(!p_video->externally_clocked);

  video_advance_crtc_timing(p_video);

  /* If rendering is inactive, make it active again if we've hit a wall time
   * vsync. If fast mode persists, it'll go inactive immediately after the
   * next paint at the next vsync raise.
   */
  if (!p_video->is_rendering_active &&
      p_video->timer_fire_expect_vsync_start &&
      p_video->is_wall_time_vsync_hit) {
    render_vsync(p_video->p_render);
    p_video->is_rendering_active = 1;
    p_video->is_wall_time_vsync_hit = 0;
  }

  if (p_video->timer_fire_expect_vsync_start) {
    p_video->num_vsyncs++;
    assert(p_video->in_vsync);
    assert(p_video->had_vsync_this_frame);
    assert(p_video->vert_counter ==
           p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
    if (p_video->is_odd_interlace_frame) {
      assert(p_video->horiz_counter == p_video->half_r0);
    } else {
      assert(p_video->horiz_counter == 0);
    }
    assert(p_video->scanline_counter == 0);
  }
  if (p_video->timer_fire_expect_vsync_end) {
    assert(!p_video->in_vsync);
    if (p_video->is_odd_interlace_frame) {
      assert(p_video->horiz_counter == p_video->half_r0);
    } else {
      assert(p_video->horiz_counter == 0);
    }
    assert(p_video->vsync_scanline_counter == 0);
  }

  video_update_timer(p_video);
}

static void
video_mode_updated(struct video_struct* p_video) {
  /* Let the renderer know about the new mode. */
  int mode;

  int is_teletext = (p_video->video_ula_control & k_ula_teletext);
  int chars_per_line = (p_video->video_ula_control & k_ula_chars_per_line);
  int clock_speed = video_get_clock_speed(p_video);
  chars_per_line >>= k_ula_chars_per_line_shift;
  if (is_teletext) {
    mode = k_render_mode7;
  } else if (clock_speed == 1) {
    switch (chars_per_line) {
    case 1:
      mode = k_render_mode2;
      break;
    case 2:
      mode = k_render_mode1;
      break;
    case 3:
      mode = k_render_mode0;
      break;
    default:
      mode = k_render_mode0;
      break;
    }
  } else {
    assert(clock_speed == 0);
    switch (chars_per_line) {
    case 1:
      mode = k_render_mode5;
      break;
    case 2:
      mode = k_render_mode4;
      break;
    default:
      /* EMU NOTE: not clear what to render here. Probably anything will do for
       * now because it's not a defined mode.
       * This condition can occur in practice, for example half-way through
       * mode switches.
       * Also, Tricky's Frogger reliably hits here with chars_per_line == 0.
       */
      mode = k_render_mode4;
      break;
    }
  }

  p_video->render_mode = mode;
  render_set_mode(p_video->p_render, mode);
}

static void
video_CB2_changed_callback(void* p, int level, int output) {
  uint32_t address_counter;
  struct video_struct* p_video;

  if (!level || !output) {
    return;
  }

  p_video = (struct video_struct*) p;
  address_counter = p_video->address_counter;

  /* If the system VIA configures CB2 to an output and flips it low -> high,
   * the CRTC thinks it sees a real light pen pulse.
   * Needed by Pharoah's Curse to start.
   */
  if (!p_video->externally_clocked) {
    video_advance_crtc_timing(p_video);
  }

  p_video->crtc_registers[k_crtc_reg_light_pen_high] = (address_counter >> 8);
  p_video->crtc_registers[k_crtc_reg_light_pen_low] = (address_counter & 0xFF);
}

struct video_struct*
video_create(uint8_t* p_bbc_mem,
             int externally_clocked,
             struct timing_struct* p_timing,
             struct render_struct* p_render,
             struct teletext_struct* p_teletext,
             struct via_struct* p_system_via,
             void (*p_framebuffer_ready_callback)(void* p,
                                                  int do_full_paint,
                                                  int framing_changed),
             void* p_framebuffer_ready_object,
             int* p_fast_flag,
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
  p_video->p_fast_flag = p_fast_flag;
  p_video->is_framing_changed_for_render = 0;
  p_video->is_wall_time_vsync_hit = 1;
  p_video->is_rendering_active = 1;

  p_video->wall_time = 0;
  p_video->vsync_next_time = 0;
  p_video->num_vsyncs = 0;
  p_video->num_crtc_advances = 0;

  p_video->video_timer_id = timing_register_timer(p_timing,
                                                  video_timer_fired,
                                                  p_video);

  p_video->crtc_address_register = 0;

  p_video->frames_skip = 0;
  p_video->frame_skip_counter = 0;
  (void) util_get_u32_option(&p_video->frames_skip,
                             p_options->p_opt_flags,
                             "video:frames-skip=");
  p_video->render_after_each_row = util_has_option(
      p_options->p_opt_flags, "video:render-after-each-row");
  p_video->log_timer = util_has_option(p_options->p_log_flags,
                                       "video:timer");

  p_video->crtc_frames = 0;
  p_video->is_even_interlace_frame = 1;
  p_video->is_odd_interlace_frame = 0;

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
  p_video->hsync_pulse_width = 4;
  p_video->vsync_pulse_width = 2;
  p_video->crtc_registers[k_crtc_reg_sync_width] =
      (p_video->hsync_pulse_width | (p_video->vsync_pulse_width << 4));
  p_video->crtc_registers[k_crtc_reg_vert_total] = 30;
  p_video->crtc_registers[k_crtc_reg_vert_adjust] = 2;
  p_video->crtc_registers[k_crtc_reg_vert_displayed] = 25;
  p_video->crtc_registers[k_crtc_reg_vert_sync_position] = 27;
  /* Interlace sync and video, 1 character display delay, 2 character cursor
   * delay.
   */
  p_video->crtc_registers[k_crtc_reg_interlace] = (3 | (1 << 4) | (2 << 6));
  p_video->crtc_registers[k_crtc_reg_lines_per_character] = 18;

  /* Set correctly as per above register values. */
  p_video->is_interlace = 1;
  p_video->is_interlace_sync_and_video = 1;
  p_video->scanline_stride = 2;
  p_video->scanline_mask = 0x1E;
  p_video->is_master_display_enable = 1;
  p_video->half_r0 = 32;

  /* Teletext mode, 1MHz operation. */
  p_video->video_ula_control = k_ula_teletext;

  if (p_system_via) {
    via_set_CB2_changed_callback(p_system_via,
                                 video_CB2_changed_callback,
                                 p_video);
  }

  video_mode_updated(p_video);

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

uint64_t
video_get_num_vsyncs(struct video_struct* p_video) {
  return p_video->num_vsyncs;
}

uint64_t
video_get_num_crtc_advances(struct video_struct* p_video) {
  return p_video->num_crtc_advances;
}

struct render_struct*
video_get_render(struct video_struct* p_video) {
  return p_video->p_render;
}

void
video_apply_wall_time_delta(struct video_struct* p_video, uint64_t delta) {
  struct via_struct* p_system_via;
  uint64_t wall_time;

  wall_time = (p_video->wall_time + delta);
  p_video->wall_time = wall_time;

  if (wall_time < p_video->vsync_next_time) {
    return;
  }

  while (p_video->vsync_next_time <= wall_time) {
    p_video->vsync_next_time += k_video_us_per_vsync;
  }

  if (p_video->frame_skip_counter == 0) {
    /* In accurate + fast mode, we use the wall time 50Hz tick to decide
     * which super fast virtual frames to render and which (the majority) to
     * not render and just maintain timing for.
     */
    p_video->is_wall_time_vsync_hit = 1;
    p_video->frame_skip_counter = p_video->frames_skip;
  } else {
    p_video->frame_skip_counter--;
  }

  if (!p_video->externally_clocked) {
    return;
  }

  p_system_via = p_video->p_system_via;
  via_set_CA1(p_system_via, 0);
  via_set_CA1(p_system_via, 1);

  if (!p_video->is_wall_time_vsync_hit) {
    return;
  }

  video_do_paint(p_video);
  p_video->is_wall_time_vsync_hit = 0;
  p_video->num_vsyncs++;
}

void
video_render_full_frame(struct video_struct* p_video) {
  uint32_t i_cols;
  uint32_t i_lines;
  uint32_t i_rows;
  uint32_t crtc_line_address;

  struct render_struct* p_render = p_video->p_render;
  void (*func_render_data)(struct render_struct*, uint8_t) =
      render_get_render_data_function(p_render);
  void (*func_render_blank)(struct render_struct*, uint8_t) =
      render_get_render_blank_function(p_render);

  /* This render function is typically called on a different thread to the
   * BBC thread, so make sure to reload register values from memory.
   */
  volatile uint8_t* p_regs = (volatile uint8_t*) &p_video->crtc_registers;

  uint32_t crtc_start_address = ((p_regs[k_crtc_reg_mem_addr_high] << 8) |
                                 p_regs[k_crtc_reg_mem_addr_low]);
  uint32_t screen_wrap_add = p_video->screen_wrap_add;
  uint32_t num_rows = p_regs[k_crtc_reg_vert_displayed];
  uint32_t num_lines = p_regs[k_crtc_reg_lines_per_character];
  uint32_t num_cols = p_regs[k_crtc_reg_horiz_displayed];
  uint32_t num_pre_lines = 0;
  uint32_t num_pre_cols = 0;
  uint8_t* p_bbc_mem = p_video->p_bbc_mem;
  struct teletext_struct* p_teletext = p_video->p_teletext;

  if ((p_regs[k_crtc_reg_interlace] & 0x03) == 0x03) {
    num_lines += 2;
    num_lines /= 2;
  } else {
    num_lines += 1;
  }
  if (p_regs[k_crtc_reg_vert_total] > p_regs[k_crtc_reg_vert_sync_position]) {
    num_pre_lines = (p_regs[k_crtc_reg_vert_total] -
                     p_regs[k_crtc_reg_vert_sync_position]);
    num_pre_lines *= num_lines;
  }
  num_pre_lines += p_regs[k_crtc_reg_vert_adjust];
  if (p_regs[k_crtc_reg_horiz_total] > p_regs[k_crtc_reg_horiz_position]) {
    num_pre_cols = (p_regs[k_crtc_reg_horiz_total] -
                    p_regs[k_crtc_reg_horiz_position]);
  }

  render_vsync(p_render);
  teletext_VSYNC_changed(p_teletext, 0);

  for (i_lines = 0; i_lines < num_pre_lines; ++i_lines) {
    render_hsync(p_render);
  }
  for (i_rows = 0; i_rows < num_rows; ++i_rows) {
    for (i_lines = 0; i_lines < num_lines; ++i_lines) {
      crtc_line_address = (crtc_start_address + (i_rows * num_cols));
      for (i_cols = 0; i_cols < num_pre_cols; ++i_cols) {
        func_render_blank(p_render, 0x00);
      }
      for (i_cols = 0; i_cols < num_cols; ++i_cols) {
        uint32_t bbc_address;
        crtc_line_address &= 0x3FFF;
        bbc_address = video_calculate_bbc_address(NULL,
                                                  crtc_line_address,
                                                  i_lines,
                                                  screen_wrap_add);
        func_render_data(p_render, p_bbc_mem[bbc_address]);
        crtc_line_address++;
      }
      render_hsync(p_render);
      teletext_DISPMTG_changed(p_teletext, 0);
    }
  }
}

static void
video_update_real_color(struct video_struct* p_video, uint8_t index) {
  uint32_t color;

  uint8_t rgbf = p_video->ula_palette[index];

  /* The actual color displayed depends on the flash bit. */
  if ((rgbf & 0x8) && video_get_flash(p_video)) {
    rgbf ^= 0x7;
  }
  /* Alpha. */
  color = 0xff000000;
  /* Red. */
  if (rgbf & 0x1) {
    color |= 0x00ff0000;
  }
  /* Green. */
  if (rgbf & 0x2) {
    color |= 0x0000ff00;
  }
  /* Blue. */
  if (rgbf & 0x4) {
    color |= 0x000000ff;
  }

  render_set_palette(p_video->p_render, index, color);
}

void
video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t index;
  uint8_t rgbf;

  int old_flash;
  int new_flash;

  if (addr == 1) {
    /* Palette register. */
    if (!p_video->externally_clocked) {
      video_advance_crtc_timing(p_video);
    }

    index = (val >> 4);
    /* The xor is to map incoming color to real physical color. e.g. MOS writes
     * 7 for black, which xors to 0, which is the number for physical black.
     */
    rgbf = ((val & 0x0F) ^ 0x7);
    p_video->ula_palette[index] = rgbf;

    video_update_real_color(p_video, index);

    return;
  }

  /* Video ULA control register. */
  assert(addr == 0);

  old_flash = video_get_flash(p_video);

  if (!p_video->externally_clocked) {
    int old_clock_speed = video_get_clock_speed(p_video);
    int new_clock_speed = !!(val & k_ula_clock_speed);
    int clock_speed_changing = (new_clock_speed != old_clock_speed);

    /* If the clock speed is changing, this affects not only rendering but also
     * fundamental timing.
     */
    video_advance_crtc_timing(p_video);

    p_video->video_ula_control = val;

    if (clock_speed_changing) {
      p_video->is_framing_changed_for_render = 1;
      video_update_timer(p_video);
    }
  } else {
    p_video->video_ula_control = val;
  }

  new_flash = video_get_flash(p_video);
  if (old_flash != new_flash) {
    uint32_t i;
    for (i = 0; i < 16; ++i) {
      if (p_video->ula_palette[i] & 0x8) {
        video_update_real_color(p_video, i);
      }
    }
  }

  video_mode_updated(p_video);
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
  uint8_t reg;

  uint8_t mask = 0xFF;
  int does_not_change_framing = 0;
  uint8_t hsync_pulse_width = 0;
  uint8_t vsync_pulse_width = 0;

  if (addr == k_crtc_addr_reg) {
    p_video->crtc_address_register = (val & k_crtc_register_mask);
    return;
  }

  assert(addr == k_crtc_addr_val);

  reg = p_video->crtc_address_register;

  if (reg >= k_crtc_num_registers) {
    return;
  }

  /* NOTE: don't change CRTC state until is safe to do so below after the CRTC
   * advance.
   */
  switch (reg) {
  /* R0 */
  case k_crtc_reg_horiz_total:
    if ((val != 63) && (val != 127)) {
      log_do_log(k_log_video, k_log_unusual, "horizontal total: %u", val);
    }
    break;
  /* R3 */
  case k_crtc_reg_sync_width:
    hsync_pulse_width = (val & 0xF);
    if ((hsync_pulse_width != 8) && (hsync_pulse_width != 4)) {
      log_do_log(k_log_video,
                 k_log_unusual,
                 "hsync pulse width: %u",
                 hsync_pulse_width);
    }
    vsync_pulse_width = (val >> 4);
    if (vsync_pulse_width == 0) {
      vsync_pulse_width = 16;
    }
    if (vsync_pulse_width != 2) {
      log_do_log(k_log_video,
                 k_log_unusual,
                 "vsync pulse width: %u",
                 vsync_pulse_width);
    }
    break;
  /* R4 */
  case k_crtc_reg_vert_total:
    mask = 0x7F;
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
    break;
  /* R10 */
  case k_crtc_reg_cursor_start:
    does_not_change_framing = 1;
    mask = 0x7F;
    break;
  /* R11 */
  case k_crtc_reg_cursor_end:
    does_not_change_framing = 1;
    mask = 0x1F;
    break;
  /* R12 */
  case k_crtc_reg_mem_addr_high:
    does_not_change_framing = 1;
    mask = 0x3F;
    break;
  /* R13 */
  case k_crtc_reg_mem_addr_low:
    does_not_change_framing = 1;
    break;
  /* R14 */
  case k_crtc_reg_cursor_high:
    does_not_change_framing = 1;
    mask = 0x3F;
    break;
  /* R15 */
  case k_crtc_reg_cursor_low:
    does_not_change_framing = 1;
    break;
  case k_crtc_reg_light_pen_high:
  case k_crtc_reg_light_pen_low:
    /* Light pen registers are read only. */
    /* NOTE: returns. */
    return;
  default:
    break;
  }

  if (!p_video->externally_clocked) {
    /* TODO: If we're skipping frame rendering in fast mode, a few registers can
     * change without us needing to do expensive tick by tick CRTC emulation.
     * Most usefully, the frame memory address registers R12 and R13 don't
     * change framing. A lot of games change only these once they are up and
     * running.
     */
    video_advance_crtc_timing(p_video);
  }

  p_video->crtc_registers[reg] = (val & mask);

  switch (reg) {
  case k_crtc_reg_horiz_total:
    p_video->half_r0 = ((p_video->crtc_registers[k_crtc_reg_horiz_total] + 1) /
                        2);
    break;
  case k_crtc_reg_sync_width:
    p_video->hsync_pulse_width = hsync_pulse_width;
    p_video->vsync_pulse_width = vsync_pulse_width;
    break;
  case k_crtc_reg_interlace:
    p_video->is_interlace = (val & 0x1);
    p_video->is_interlace_sync_and_video = ((val & 0x3) == 0x3);
    p_video->is_master_display_enable = ((val & 0x30) != 0x30);
    if (p_video->is_interlace_sync_and_video) {
      p_video->scanline_stride = 2;
      p_video->scanline_mask = 0x1E;
    } else {
      p_video->scanline_stride = 1;
      p_video->scanline_mask = 0x1F;
    }
    video_update_odd_even_frame(p_video);
    break;
  default:
    break;
  }

  if (p_video->is_interlace_sync_and_video) {
    /* EMU NOTE: interlace sync and video has a different behavior when
     * programmed with an odd number in R9.
     * The Hitachi datasheet covers it on page "92":
     * https://www.cpcwiki.eu/imgs/c/c0/Hd6845.hitachi.pdf
     * Since MODE7 doesn't use it, it's simplest to not emulate it, and this
     * helps avoid headaches when CRTC registers are half-way programmed
     * from MODE7 -> non-MODE7.
     */
     p_video->scanline_counter &= ~1;
  }

  if (!does_not_change_framing) {
    if (!p_video->externally_clocked) {
      video_update_timer(p_video);
    }
    p_video->is_framing_changed_for_render = 1;
  }
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
    p_values[i] = p_video->ula_palette[i];
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

void
video_get_crtc_state(struct video_struct* p_video,
                     uint8_t* p_horiz_counter,
                     uint8_t* p_scanline_counter,
                     uint8_t* p_vert_counter,
                     uint16_t* p_address_counter) {
  if (!p_video->externally_clocked) {
    video_advance_crtc_timing(p_video);
  }
  *p_horiz_counter = p_video->horiz_counter;
  *p_scanline_counter = p_video->scanline_counter;
  *p_vert_counter = p_video->vert_counter;
  *p_address_counter = (uint16_t) p_video->address_counter;
}

#include "test-video.c"
