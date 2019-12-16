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
  int is_framing_changed;
  int is_wall_time_vsync_hit;
  int is_rendering_active;

  /* Options. */
  uint32_t frames_skip;
  uint32_t frame_skip_counter;
  int render_after_each_row;

  /* Timing. */
  uint64_t wall_time;
  uint64_t vsync_next_time;
  uint64_t prev_system_ticks;
  int timer_fire_expect_vsync_start;
  int timer_fire_expect_vsync_end;
  int clock_speed_changing;

  /* Video ULA state. */
  uint8_t video_ula_control;
  uint8_t ula_palette[16];
  uint32_t screen_wrap_add;

  /* 6845 registers and derivatives. */
  uint8_t crtc_address_register;
  uint8_t crtc_registers[k_crtc_num_registers];
  int is_interlace_sync_and_video;
  int is_master_display_enable;
  uint8_t hsync_pulse_width;
  uint8_t vsync_pulse_width;

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
}

static inline int
video_get_clock_speed(struct video_struct* p_video) {
  return !!(p_video->video_ula_control & k_ula_clock_speed);
}

static void
video_do_paint(struct video_struct* p_video) {
  int do_full_render = p_video->externally_clocked;
  /* TODO: make MODE7 work with the CRTC implementation. */
  if (p_video->render_mode == k_render_mode7) {
    do_full_render = 1;
  }
  p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object,
                                        do_full_render,
                                        p_video->is_framing_changed);
  p_video->is_framing_changed = 0;

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
  p_video->in_vsync = 1;
  p_video->had_vsync_this_frame = 1;
  p_video->vsync_scanline_counter = p_video->vsync_pulse_width;
  via_set_CA1(p_video->p_system_via, 1);
}

static void
video_set_vsync_lower_state(struct video_struct* p_video) {
  p_video->in_vsync = 0;
  via_set_CA1(p_video->p_system_via, 0);
}

static void
video_advance_crtc_timing(struct video_struct* p_video) {
  uint8_t scanline_stride;
  uint8_t scanline_mask;
  uint32_t bbc_address;
  uint8_t data;
  uint64_t advance_to_system_ticks;
  uint64_t tick_to_system_ticks;
  uint64_t delta_crtc_ticks;

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
  int odd_ticks = (curr_system_ticks & 1);
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

  func_render = func_render_blank;
  if (video_is_display_enabled(p_video)) {
    func_render = func_render_data;
  }

  if (p_video->is_interlace_sync_and_video) {
    scanline_stride = 2;
    /* EMU NOTE: see comment in video_crtc_write. */
    scanline_mask = 0x1E;
  } else {
    scanline_stride = 1;
    scanline_mask = 0x1F;
  }

  advance_to_system_ticks = curr_system_ticks;
  tick_to_system_ticks = curr_system_ticks;

  if (p_video->clock_speed_changing && odd_ticks) {
    /* EMU: switching clock speeds on an odd system tick is a tricky operation
     * in software and hardware.
     * In both cases, the timing tweaks below effectively stretch one tick
     * out a bit in order to keep things in order.
     */
    if (clock_speed == 0) {
      /* About to switch 1MHz -> 2MHz. */
      tick_to_system_ticks--;
    } else {
      /* About to switch 2MHz -> 1MHz. */
      advance_to_system_ticks--;
      tick_to_system_ticks--;
    }
  } else if ((clock_speed == 0) && odd_ticks) {
    /* In 1MHz mode we might be advancing CRTC time at the half-cycle point.
     * This can occur for example upon a video ULA change (palette or control
     * registers), because the video ULA runs at 2MHz for register updates.
     */
    advance_to_system_ticks--;
    tick_to_system_ticks--;
  }

  delta_crtc_ticks = (tick_to_system_ticks - p_video->prev_system_ticks);

  if (clock_speed == 0) {
    assert(!(p_video->prev_system_ticks & 1));
    assert(!(delta_crtc_ticks & 1));
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
    p_video->display_enable_horiz = 1;

    if (p_video->in_vsync) {
      p_video->vsync_scanline_counter--;
      if (p_video->vsync_scanline_counter == 0) {
        video_set_vsync_lower_state(p_video);
      }
    }

    r9_hit = (p_video->scanline_counter == (r9 & scanline_mask));
    p_video->scanline_counter = ((p_video->scanline_counter + scanline_stride) &
                                 scanline_mask);
    p_video->address_counter = p_video->address_counter_this_row;

    func_render = video_is_display_enabled(p_video) ?
        func_render_data : func_render_blank;

    if (p_video->in_vert_adjust) {
      p_video->vert_adjust_counter++;
      p_video->vert_adjust_counter &= 0x1F;
      r5_hit = (p_video->vert_adjust_counter == r5);
      if (r5_hit) {
        goto start_new_frame;
      }
      continue;
    }

    if (!r9_hit) {
      continue;
    }

    /* End of character row. */
    p_video->scanline_counter = 0;
    p_video->address_counter = p_video->address_counter_next_row;
    p_video->address_counter_this_row = p_video->address_counter_next_row;

    r4_hit = (p_video->vert_counter == r4);
    p_video->vert_counter = ((p_video->vert_counter + 1) & 0x7F);
    r6_hit = (p_video->vert_counter == r6);
    r7_hit = (p_video->vert_counter == r7);

    if (r6_hit) {
      p_video->display_enable_vert = 0;
    }
    if (r7_hit && !p_video->had_vsync_this_frame) {
      video_set_vsync_raise_state(p_video);

      video_do_paint(p_video);

      render_vsync(p_render);
    }

    func_render = video_is_display_enabled(p_video) ?
        func_render_data : func_render_blank;

    if (!r4_hit) {
      continue;
    }

    /* End of R4-based frame. Time for either a new frame or vertical adjust. */
    if (r5 != 0) {
      p_video->in_vert_adjust = 1;
      continue;
    }

start_new_frame:
    video_start_new_frame(p_video);
    func_render = video_is_display_enabled(p_video) ?
        func_render_data : func_render_blank;
  }

  p_video->clock_speed_changing = 0;
  p_video->prev_system_ticks = advance_to_system_ticks;
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
  uint32_t r7;
  uint32_t r9;

  uint32_t tick_multiplier;
  uint32_t scanline_ticks;
  uint32_t ticks_to_next_scanline;
  uint32_t ticks_to_next_row;
  uint32_t scanline_stride;
  uint32_t scanlines_per_row;
  uint32_t scanlines_left_this_row;
  uint32_t vert_counter_max;

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
  r7 = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  r9 = p_video->crtc_registers[k_crtc_reg_lines_per_character];

  vert_counter_max = r4;
  if (r5 > 0) {
    vert_counter_max++;
  }

  if (p_video->is_interlace_sync_and_video) {
    scanline_stride = 2;
  } else {
    scanline_stride = 1;
  }

  tick_multiplier = 1;
  if (video_get_clock_speed(p_video) == 0) {
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
  if (p_video->in_vert_adjust) {
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
    scanlines_left_this_row /= scanline_stride;
  }

  scanlines_per_row = (r9 / scanline_stride);
  scanlines_per_row++;

  scanline_ticks *= tick_multiplier;
  ticks_to_next_scanline *= tick_multiplier;
  ticks_to_next_row = ticks_to_next_scanline;
  ticks_to_next_row += (scanline_ticks * scanlines_left_this_row);

//printf("hc %d sc %d ac %d vc %d r4 %d r5 %d r7 %d r9 %d adj %d isv %d mult %d ticks %zu\n", p_video->horiz_counter, p_video->scanline_counter, p_video->vert_adjust_counter, p_video->vert_counter, r4, r5, r7, r9, p_video->in_vert_adjust, p_video->is_interlace_sync_and_video, tick_multiplier, timing_get_total_timer_ticks(p_video->p_timing));

  if (p_video->in_vsync) {
    assert(p_video->vsync_scanline_counter > 0);
    timer_value = ticks_to_next_scanline;
    timer_value += ((p_video->vsync_scanline_counter - 1) * scanline_ticks);
    p_video->timer_fire_expect_vsync_end = 1;
  } else if ((p_video->vert_counter < r7) &&
             (r7 <= vert_counter_max) &&
             !p_video->had_vsync_this_frame) {
//printf("vc < r7, r7 %d\n", r7);
    /* In this branch, vsync will happen this current frame. */
    assert(!p_video->in_vert_adjust);
    timer_value = ticks_to_next_row;
    timer_value += (scanline_ticks *
                    scanlines_per_row *
                    (r7 - p_video->vert_counter - 1));
    p_video->timer_fire_expect_vsync_start = 1;
  } else if (r7 > r4) {
    /* If vsync'ing isn't happening, just wake up every 50Hz or so. */
    timer_value = (k_video_us_per_vsync * 2);
  } else {
    /* Vertical counter is already past the vsync position, so we need to
     * calculate the timing to the end of the frame, plus the timing from
     * start of frame to vsync.
     */
    uint32_t ticks_to_end_of_frame;
    uint32_t ticks_from_frame_to_vsync;

//printf("vc >= r7, r7 = %d\n", r7);
    ticks_from_frame_to_vsync = (scanline_ticks * scanlines_per_row * r7);
    ticks_to_end_of_frame = ticks_to_next_row;
    if (!p_video->in_vert_adjust) {
      uint32_t rows_to_end_of_frame;

      if (p_video->vert_counter <= r4) {
        rows_to_end_of_frame = (r4 - p_video->vert_counter);
      } else {
        rows_to_end_of_frame = (0x7F - p_video->vert_counter);
        rows_to_end_of_frame += (r4 + 1);
      }
      ticks_to_end_of_frame += (scanline_ticks *
                                scanlines_per_row *
                                rows_to_end_of_frame);
      ticks_to_end_of_frame += (scanline_ticks * r5);
    }

    timer_value = (ticks_to_end_of_frame + ticks_from_frame_to_vsync);
    p_video->timer_fire_expect_vsync_start = 1;
  }

  assert(timer_value < (4 * 1024 * 1024));

//printf("timer value: %zu\n", timer_value);

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

  assert(timing_get_timer_value(p_video->p_timing, p_video->video_timer_id) ==
         0);
  assert(!p_video->externally_clocked);

//printf("timer fired\n");

  /* If rendering is inactive, make it active again if we've hit a wall time
   * vsync. If fast mode persists, it'll go inactive immediately after the
   * render + paint in the video_advance_crtc_timing() call below.
   */
  if (!p_video->is_rendering_active &&
      p_video->timer_fire_expect_vsync_end &&
      p_video->is_wall_time_vsync_hit) {
    p_video->is_rendering_active = 1;
    p_video->is_wall_time_vsync_hit = 0;
  }

  if (p_video->is_rendering_active) {
    video_advance_crtc_timing(p_video);
  } else {
    /* If we're in fast mode and skipping rendering, we'll need to short
     * circuit the CRTC advance directly to the correct state.
     */
    assert(p_video->horiz_counter == 0);
    assert(p_video->vert_counter ==
           p_video->crtc_registers[k_crtc_reg_vert_sync_position]);

    /* TODO: could happen. Not handled yet. Probably best to check for unusual
     * situations before deactivating rendering.
     */
    assert(!p_video->in_vert_adjust);
    assert(p_video->vsync_pulse_width <=
           p_video->crtc_registers[k_crtc_reg_lines_per_character]);

    if (p_video->timer_fire_expect_vsync_start) {
      assert(p_video->scanline_counter == p_video->vsync_pulse_width);
      assert(p_video->vsync_scanline_counter == 0);
      video_set_vsync_raise_state(p_video);
      p_video->scanline_counter = 0;
    } else {
      assert(p_video->timer_fire_expect_vsync_end);
      assert(p_video->in_vsync);
      assert(p_video->had_vsync_this_frame);
      assert(p_video->scanline_counter == 0);
      assert(p_video->vsync_scanline_counter == p_video->vsync_pulse_width);
      video_set_vsync_lower_state(p_video);
      p_video->vsync_scanline_counter = 0;
      p_video->scanline_counter = p_video->vsync_pulse_width;
    }

    p_video->prev_system_ticks =
        timing_get_scaled_total_timer_ticks(p_video->p_timing);
  }

  if (p_video->timer_fire_expect_vsync_start) {
    assert(p_video->in_vsync);
    assert(p_video->had_vsync_this_frame);
    assert(p_video->vert_counter ==
           p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
    assert(p_video->horiz_counter == 0);
    assert(p_video->scanline_counter == 0);
  }
  if (p_video->timer_fire_expect_vsync_end) {
    assert(!p_video->in_vsync);
    assert(p_video->horiz_counter == 0);
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
  p_video->is_framing_changed = 0;
  p_video->is_wall_time_vsync_hit = 1;
  p_video->is_rendering_active = 1;

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
  p_video->render_after_each_row = util_has_option(
      p_options->p_opt_flags, "video:render-after-each-row");

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
  p_video->is_interlace_sync_and_video = 1;
  p_video->is_master_display_enable = 1;

  /* Teletext mode, 1MHz operation. */
  p_video->video_ula_control = k_ula_teletext;

  via_set_CB2_changed_callback(p_system_via,
                               video_CB2_changed_callback,
                               p_video);

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
  if (!p_video->externally_clocked && p_video->is_rendering_active) {
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

void
video_render_full_frame(struct video_struct* p_video) {
  int clock_speed;
  size_t horiz_chars;
  size_t vert_chars;
  size_t vert_lines;
  int horiz_chars_offset;
  int vert_lines_offset;

  size_t max_horiz_chars;
  size_t max_vert_lines;

  struct render_struct* p_render = p_video->p_render;
  uint32_t* p_render_buffer = render_get_buffer(p_render);
  int mode = p_video->render_mode;

  if (p_render_buffer == NULL) {
    return;
  }

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

  switch (mode) {
  case k_render_mode7:
    /* NOTE: what happens with crazy combinations, such as 2MHz clock here? */
    teletext_render_full(p_video->p_teletext, p_video);
    break;
  case k_render_mode0:
  case k_render_mode1:
  case k_render_mode2:
    video_2MHz_mode_render(p_video,
                           mode,
                           horiz_chars,
                           vert_chars,
                           horiz_chars_offset,
                           vert_lines_offset);
    break;
  case k_render_mode4:
  case k_render_mode5:
    video_1MHz_mode_render(p_video,
                           mode,
                           horiz_chars,
                           vert_chars,
                           horiz_chars_offset,
                           vert_lines_offset);
    break;
  default:
    assert(0);
    break;
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
    if (!p_video->externally_clocked && p_video->is_rendering_active) {
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

    p_video->clock_speed_changing = clock_speed_changing;

    /* If the clock speed is changing, this affects not only rendering but also
     * fundamental timing.
     */
    if (clock_speed_changing || p_video->is_rendering_active) {
      video_advance_crtc_timing(p_video);
    }

    p_video->video_ula_control = val;

    if (clock_speed_changing) {
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
  uint8_t hsync_pulse_width;
  uint8_t vsync_pulse_width;
  uint8_t reg;

  uint8_t mask = 0xFF;
  int does_not_change_framing = 0;

  if (addr == k_crtc_addr_reg) {
    p_video->crtc_address_register = (val & k_crtc_register_mask);
    return;
  }

  assert(addr == k_crtc_addr_val);

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
    hsync_pulse_width = (val & 0xF);
    if ((hsync_pulse_width != 8) && (hsync_pulse_width != 4)) {
      printf("LOG:CRTC:unusual hsync pulse width: %d\n", hsync_pulse_width);
    }
    vsync_pulse_width = (val >> 4);
    if (vsync_pulse_width == 0) {
      vsync_pulse_width = 16;
    }
    if (vsync_pulse_width != 2) {
      printf("LOG:CRTC:unusual vsync pulse width: %d\n", vsync_pulse_width);
    }
    p_video->hsync_pulse_width = hsync_pulse_width;
    p_video->vsync_pulse_width = vsync_pulse_width;
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
  case k_crtc_reg_interlace:
    p_video->is_interlace_sync_and_video = ((val & 0x3) == 0x3);
    p_video->is_master_display_enable = ((val & 0x30) != 0x30);
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
  default:
    break;
  }

  if (!p_video->externally_clocked) {
    /* If we're skipping frame rendering in fast mode, a few registers can
     * change without us needing to do expensive tick by tick CRTC emulation.
     * Most usefully, the frame memory address registers R12 and R13 don't
     * change framing. A lot of games change only these once they are up and
     * running.
     */
    if (!does_not_change_framing || p_video->is_rendering_active) {
      video_advance_crtc_timing(p_video);
    }
  }

  p_video->crtc_registers[reg] = (val & mask);

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
    p_video->is_framing_changed = 1;
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
