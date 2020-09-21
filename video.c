#include "video.h"

#include "bbc_options.h"
#include "log.h"
#include "render.h"
#include "teletext.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
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
  uint8_t* p_shadow_mem;
  int externally_clocked;
  struct timing_struct* p_timing;
  struct teletext_struct* p_teletext;
  struct via_struct* p_system_via;
  uint32_t timer_id;
  void (*p_framebuffer_ready_callback)(void*, int, int);
  void* p_framebuffer_ready_object;
  int* p_fast_flag;

  /* Rendering. */
  struct render_struct* p_render;
  int is_framing_changed_for_render;
  int is_wall_time_vsync_hit;
  int is_rendering_active;

  /* Options. */
  uint32_t frames_skip;
  uint32_t frame_skip_counter;
  uint32_t render_every_ticks;

  /* Timing. */
  uint64_t wall_time;
  uint64_t vsync_next_time;
  uint64_t prev_system_ticks;
  int timer_fire_force_vsync_start;
  int timer_fire_force_vsync_end;
  uint64_t num_vsyncs;
  uint64_t num_crtc_advances;

  /* Video ULA state and derivatives. */
  uint8_t video_ula_control;
  uint8_t ula_palette[16];
  uint32_t screen_wrap_add;
  uint32_t clock_tick_multiplier;
  int is_shadow_displayed;
  int is_shadow_paged;

  /* 6845 registers and derivatives. */
  uint8_t crtc_address_register;
  uint8_t crtc_registers[k_video_crtc_num_registers];
  int is_interlace;
  int is_interlace_sync_and_video;
  int is_master_display_enable;
  uint32_t scanline_stride;
  uint32_t scanline_mask;
  uint8_t hsync_pulse_width;
  uint8_t vsync_pulse_width;
  uint8_t half_r0;
  int cursor_disabled;
  int cursor_flashing;
  uint32_t cursor_flash_mask;
  uint8_t cursor_start_line;
  int has_sane_framing_parameters;
  int32_t frame_crtc_ticks;

  /* 6845 state. */
  uint64_t crtc_frames;
  int is_even_interlace_frame;
  int is_odd_interlace_frame;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint8_t vert_adjust_counter;
  uint8_t vsync_scanline_counter;
  uint8_t hsync_tick_counter;
  uint32_t address_counter;
  uint32_t address_counter_this_row;
  uint32_t address_counter_next_row;
  int in_vert_adjust;
  int in_vsync;
  int in_hsync;
  int in_dummy_raster;
  int had_vsync_this_row;
  int do_dummy_raster;
  int display_enable_horiz;
  int display_enable_vert;
  int has_hit_cursor_line_start;
  int has_hit_cursor_line_end;
  int is_end_of_main_latched;
  int is_end_of_frame_latched;
  uint32_t start_of_line_state_checks;
  int is_first_frame_scanline;
};

static inline uint32_t
video_calculate_bbc_address(uint32_t address_counter,
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

  return address;
}

static inline int
video_is_display_enabled(struct video_struct* p_video) {
  int enabled = p_video->is_master_display_enable;
  enabled &= p_video->display_enable_horiz;
  enabled &= p_video->display_enable_vert;

  return enabled;
}

static inline void
video_start_new_frame(struct video_struct* p_video) {
  uint32_t address_counter;

  p_video->horiz_counter = 0;
  p_video->scanline_counter = 0;
  p_video->vert_counter = 0;
  p_video->vert_adjust_counter = 0;

  p_video->had_vsync_this_row = 0;
  p_video->in_vert_adjust = 0;
  p_video->in_dummy_raster = 0;
  p_video->do_dummy_raster = 0;
  p_video->has_hit_cursor_line_start = 0;
  p_video->has_hit_cursor_line_end = 0;
  p_video->is_end_of_main_latched = 0;
  p_video->is_end_of_frame_latched = 0;
  p_video->start_of_line_state_checks = 1;
  p_video->is_first_frame_scanline = 1;

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
  int do_full_render;

  /* If we're in fast mode and internally clocked, give rendering and painting
   * a rest after each paint consideration.
   * We'll get prodded to start again by the 50Hz real time tick, which will
   * get noticed in video_timer_fired().
   */
  if (!p_video->externally_clocked && *p_video->p_fast_flag) {
    p_video->is_rendering_active = 0;
    p_video->is_wall_time_vsync_hit = 0;
  }

  /* Skip the paint for the appropriate option. */
  if (p_video->frame_skip_counter > 0) {
    p_video->frame_skip_counter--;
    return;
  }

  p_video->frame_skip_counter = p_video->frames_skip;

  do_full_render = p_video->externally_clocked;
  p_video->p_framebuffer_ready_callback(p_video->p_framebuffer_ready_object,
                                        do_full_render,
                                        p_video->is_framing_changed_for_render);
  p_video->is_framing_changed_for_render = 0;
}

static void
video_flyback_callback(void* p) {
  struct video_struct* p_video = (struct video_struct*) p;

  assert(!p_video->externally_clocked);

  if (p_video->is_rendering_active) {
    video_do_paint(p_video);
  }
}

static void
video_set_vsync_raise_state(struct video_struct* p_video) {
  struct via_struct* p_system_via = p_video->p_system_via;

  assert(!p_video->in_vsync);
  assert(!p_video->had_vsync_this_row);
  assert(p_video->vsync_scanline_counter == 0);

  p_video->num_vsyncs++;
  p_video->in_vsync = 1;
  p_video->had_vsync_this_row = 1;
  p_video->vsync_scanline_counter = p_video->vsync_pulse_width;
  p_video->do_dummy_raster = p_video->is_odd_interlace_frame;
  if (p_system_via) {
    via_set_CA1(p_system_via, 1);
  }

  if (p_video->is_rendering_active) {
    /* Painting occurs in the renderer's flyback callback;
     * see video_flyback_callback().
     */
    render_vsync(p_video->p_render, 1);
  }
}

static void
video_set_vsync_lower_state(struct video_struct* p_video) {
  struct via_struct* p_system_via = p_video->p_system_via;

  assert(p_video->in_vsync);
  assert(p_video->vsync_scanline_counter == 0);

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
      !p_video->had_vsync_this_row &&
      !p_video->in_vsync) {
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
  uint32_t cursor_addr =
      ((p_video->crtc_registers[k_crtc_reg_cursor_high] << 8) |
       p_video->crtc_registers[k_crtc_reg_cursor_low]);

  void (*func_render_data)(struct render_struct*, uint8_t) =
      render_get_render_data_function(p_render);
  void (*func_render_blank)(struct render_struct*, uint8_t) =
      render_get_render_blank_function(p_render);

  if (p_video->externally_clocked) {
    return;
  }

  p_video->timer_fire_force_vsync_start = 0;
  p_video->timer_fire_force_vsync_end = 0;

  p_video->num_crtc_advances++;

  delta_crtc_ticks = (curr_system_ticks - p_video->prev_system_ticks);
  assert(delta_crtc_ticks < INT_MAX);

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

  goto check_r6;

  while (delta_crtc_ticks--) {
    r0_hit = (p_video->horiz_counter == r0);
    r1_hit = (p_video->horiz_counter == r1);

    if (p_video->start_of_line_state_checks) {
      if (p_video->start_of_line_state_checks & 4) {
        /* One tick after the end-of-main check is the end-of-vert-adjust
         * check.
         */
        if (p_video->is_end_of_main_latched) {
          if (r5_hit) {
            p_video->is_end_of_frame_latched = 1;
          } else {
            p_video->in_vert_adjust = 1;
          }
        }
        p_video->start_of_line_state_checks &= ~4;
      }
      if (p_video->start_of_line_state_checks & 2) {
        /* One tick after the new line (typically C0=1), end-of-main is checked
         * and latched.
         */
        if (r4_hit && r9_hit) {
          p_video->is_end_of_main_latched = 1;
        }
        p_video->start_of_line_state_checks &= ~2;
        p_video->start_of_line_state_checks |= 4;
      }
      if (p_video->start_of_line_state_checks & 1) {
        p_video->start_of_line_state_checks &= ~1;
        p_video->start_of_line_state_checks |= 2;
      }
    }

    /* Wraps 0xFF -> 0; uint8_t type. */
    p_video->horiz_counter++;

    if (r1_hit) {
      p_video->display_enable_horiz = 0;
      func_render = func_render_blank;
      p_video->address_counter_next_row = p_video->address_counter;
      if (p_video->display_enable_vert) {
        teletext_DISPMTG_changed(p_video->p_teletext, 0);
      }
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

    if (p_video->is_rendering_active) {
      /* This ugly -1 here is just a code ordering issue related to an
       * optimization; we already incremented horiz_counter above.
       */
      if (p_video->in_hsync) {
        p_video->hsync_tick_counter--;
        if (p_video->hsync_tick_counter == 0) {
          p_video->in_hsync = 0;
        }
      } else {
        r2_hit = (((uint8_t) (p_video->horiz_counter - 1)) == r2);
        if (r2_hit && (p_video->hsync_pulse_width > 0)) {
          render_hsync(p_render, (p_video->hsync_pulse_width *
                                  p_video->clock_tick_multiplier));
          p_video->in_hsync = 1;
          p_video->hsync_tick_counter = p_video->hsync_pulse_width;
        }
      }
      if (!p_video->cursor_disabled &&
          (p_video->address_counter == cursor_addr) &&
          p_video->has_hit_cursor_line_start &&
          !p_video->has_hit_cursor_line_end &&
          (!p_video->cursor_flashing || (p_video->crtc_frames &
                                         p_video->cursor_flash_mask)) &&
          p_video->display_enable_horiz &&
          p_video->display_enable_vert) {
        /* EMU: cursor _does_ display if master display enable is off, but it's
         * within the horiz / vert border.
         */
        render_cursor(p_render);
      }

      if (!r0_hit) {
        bbc_address = video_calculate_bbc_address(p_video->address_counter,
                                                  p_video->scanline_counter,
                                                  p_video->screen_wrap_add);
        data = p_bbc_mem[bbc_address];
        func_render(p_render, data);
      }
    }

    p_video->address_counter = ((p_video->address_counter + 1) & 0x3FFF);

    if (!r0_hit) {
      continue;
    }

    /* End of horizontal line.
     * There's no display output for this last character.
     */
    if (p_video->is_rendering_active) {
      func_render_blank(p_render, 0);
    }
    p_video->horiz_counter = 0;
    p_video->display_enable_horiz = 1;
    p_video->is_first_frame_scanline = 0;
    if (p_video->scanline_counter ==
        p_video->crtc_registers[k_crtc_reg_cursor_end]) {
      p_video->has_hit_cursor_line_end = 1;
    }

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

    /* End-of-line state transitions. */
    if (p_video->in_dummy_raster) {
      video_start_new_frame(p_video);
      goto check_r7;
    } else if (p_video->is_end_of_frame_latched) {
      if (p_video->do_dummy_raster) {
        p_video->in_dummy_raster = 1;
      } else {
        video_start_new_frame(p_video);
        goto check_r7;
      }
    }

    /* Start the new line state check chain. */
    p_video->start_of_line_state_checks |= 1;

    r9_hit = (p_video->scanline_counter == (r9 & p_video->scanline_mask));
    p_video->scanline_counter += p_video->scanline_stride;
    p_video->scanline_counter &= p_video->scanline_mask;
    p_video->address_counter = p_video->address_counter_this_row;

    if (p_video->in_vert_adjust) {
      p_video->vert_adjust_counter++;
      p_video->vert_adjust_counter &= 0x1F;
    }

    if (r9_hit) {
      /* End of character row. */
      p_video->scanline_counter = 0;
      p_video->address_counter = p_video->address_counter_next_row;
      p_video->address_counter_this_row = p_video->address_counter_next_row;
      p_video->has_hit_cursor_line_start = 0;
      p_video->has_hit_cursor_line_end = 0;
      p_video->had_vsync_this_row = 0;

      p_video->vert_counter = ((p_video->vert_counter + 1) & 0x7F);
    }

check_r6:
    r6_hit = (p_video->vert_counter == r6);
    if (r6_hit &&
        p_video->display_enable_vert &&
        !p_video->is_first_frame_scanline) {
      p_video->display_enable_vert = 0;
      /* On the Hitachi 6845, frame counting is done on R6 hit. */
      p_video->crtc_frames++;
      video_update_odd_even_frame(p_video);
    }

check_r7:
    check_vsync_at_half_r0 = video_is_check_vsync_at_half_r0(p_video);
    r7_hit = (p_video->vert_counter == r7);
    if (r7_hit &&
        !p_video->is_odd_interlace_frame &&
        !p_video->had_vsync_this_row &&
        !p_video->in_vsync) {
      assert(!check_vsync_at_half_r0);

      video_set_vsync_raise_state(p_video);
    }

    if (p_video->scanline_counter == p_video->cursor_start_line) {
      p_video->has_hit_cursor_line_start = 1;
    }
    render_set_RA(p_render, p_video->scanline_counter);
    func_render = video_is_display_enabled(p_video) ?
        func_render_data : func_render_blank;

    r4_hit = (p_video->vert_counter == r4);
    r5_hit = (p_video->vert_adjust_counter == r5);
    r9_hit = (p_video->scanline_counter == r9);
  }

  p_video->prev_system_ticks = curr_system_ticks;
}

static void
video_init_timer(struct video_struct* p_video) {
  if (p_video->externally_clocked) {
    return;
  }
  (void) timing_start_timer_with_value(p_video->p_timing, p_video->timer_id, 0);
}

static inline int
video_get_flash(struct video_struct* p_video) {
  return !!(p_video->video_ula_control & k_ula_flash);
}

static int
video_is_at_vsync_start(struct video_struct* p_video) {
  if (p_video->vert_counter !=
      p_video->crtc_registers[k_crtc_reg_vert_sync_position]) {
    return 0;
  }
  if (p_video->scanline_counter != 0) {
    return 0;
  }
  if (!p_video->is_interlace || p_video->is_even_interlace_frame) {
    if (p_video->horiz_counter != 0) {
      return 0;
    }
  } else {
    if (p_video->horiz_counter != p_video->half_r0) {
      return 0;
    }
  }
  if (p_video->had_vsync_this_row && !p_video->in_vsync) {
    return 0;
  }
  /* TODO: not 100% correct? Probably want to maintain the system time at the
   * last vsync raise and just compare to that?
   */
  /* This can occur when to interlace sync and video, while on scanline 1,
   * which masks the scanline back to 0, causing another match on vsync
   * position.
   */
  if (p_video->vsync_scanline_counter != p_video->vsync_pulse_width) {
    return 0;
  }

  assert(p_video->in_vsync);

  return 1;
}

static int
video_is_at_vsync_end(struct video_struct* p_video) {
  if (p_video->vert_counter !=
      p_video->crtc_registers[k_crtc_reg_vert_sync_position]) {
    return 0;
  }
  if (p_video->scanline_counter == 0) {
    return 0;
  }
  if (p_video->vsync_scanline_counter != 0) {
    assert(p_video->in_vsync);
    return 0;
  }
  if (!p_video->is_interlace || p_video->is_even_interlace_frame) {
    if (p_video->horiz_counter != 0) {
      return 0;
    }
  } else {
    if (p_video->horiz_counter != p_video->half_r0) {
      return 0;
    }
  }

  assert(!p_video->in_vsync);
  return 1;
}

static uint64_t
video_calculate_timer(struct video_struct* p_video, int clock_speed) {
  uint32_t r0;
  uint32_t horiz_counter;

  uint32_t tick_multiplier = p_video->clock_tick_multiplier;

  /* If we switched to 1MHz on an odd cycle, the first CRTC tick will only take    * half the time, to re-catch the 1MHz train.
   */
  if ((clock_speed == 0) &&
      (timing_get_scaled_total_timer_ticks(p_video->p_timing) & 1)) {
    return 1;
  }

  r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];

  /* If we're at one of the vsync points of significance, and sanely framed,
   * we can easily time to the next point.
   */
  if (p_video->has_sane_framing_parameters &&
      video_is_at_vsync_start(p_video)) {
    /* Enter fast render skipping mode if appropriate. */
    if (!p_video->is_rendering_active) {
      assert(!p_video->timer_fire_force_vsync_start);
      assert(!p_video->timer_fire_force_vsync_end);
      p_video->timer_fire_force_vsync_end = 1;
    }
    return (p_video->vsync_pulse_width * (r0 + 1) * tick_multiplier);
  }
  if (p_video->has_sane_framing_parameters && video_is_at_vsync_end(p_video)) {
    uint32_t ret;

    ret = p_video->frame_crtc_ticks;
    ret -= (p_video->vsync_pulse_width * (r0 + 1));
    ret *= tick_multiplier;
    return ret;
  }

  horiz_counter = p_video->horiz_counter;

  /* If we're past R0, realign to C0=0. */
  if (horiz_counter > r0) {
    return ((256 - horiz_counter) * tick_multiplier);
  }

  /* If R7 > R4 then no point stopping much as vsync is not happening. This
   * situation does occur a lot in games using vertical rupture, e.g.
   * Uridium, Tricky's Frogger.
   * R4 + 1 is used because sync can occur at R4 + 1 with vertical adjust or
   * interlace.
   */
  if ((p_video->crtc_registers[k_crtc_reg_vert_sync_position] >
          (p_video->crtc_registers[k_crtc_reg_vert_total] + 1)) &&
      !p_video->in_vsync) {
    return k_video_us_per_vsync;
  }

  /* In interlace mode, stop at half R0. */
  if (p_video->is_interlace && (horiz_counter < p_video->half_r0)) {
    return ((p_video->half_r0 - horiz_counter) * tick_multiplier);
  }

  /* Stop at C0=0. */
  return (((r0 + 1) - horiz_counter) * tick_multiplier);
}

static void
video_update_timer(struct video_struct* p_video) {
  int clock_speed;
  uint64_t timer_value;
  uint32_t render_every_ticks;

  if (p_video->externally_clocked) {
    return;
  }

  assert(!p_video->timer_fire_force_vsync_start);
  assert(!p_video->timer_fire_force_vsync_end);

  clock_speed = video_get_clock_speed(p_video);

  timer_value = video_calculate_timer(p_video, clock_speed);
  assert(timer_value < (4 * 1024 * 1024));

  /* beebjit does not synchronize the video RAM read with the CPU RAM writes.
   * This leads to juddery display in same games where screen RAM updates are
   * carefully arranged at some useful point relative to vsync.
   * Fortess is a good example.
   * This option here renders after every so many CRTC ticks, leading to a more
   * accurate display at the cost of some performance.
   */
  render_every_ticks = p_video->render_every_ticks;
  if (render_every_ticks) {
    render_every_ticks *= p_video->clock_tick_multiplier;
    if (timer_value > render_every_ticks) {
      timer_value = render_every_ticks;
    }
  }

  (void) timing_set_timer_value(p_video->p_timing,
                                p_video->timer_id,
                                timer_value);
}

static void
video_jump_to_vsync_start(struct video_struct* p_video) {
  uint32_t timer_value;
  uint32_t r0;

  assert(p_video->has_sane_framing_parameters);
  /* Jump from previous state (vsync end) to new state (vsync start). */
  p_video->crtc_frames++;
  if (p_video->is_interlace) {
    video_update_odd_even_frame(p_video);
    if (p_video->is_odd_interlace_frame) {
      p_video->horiz_counter = p_video->half_r0;
    } else {
      p_video->horiz_counter = 0;
    }
  }
  p_video->scanline_counter = 0;
  p_video->prev_system_ticks =
      timing_get_scaled_total_timer_ticks(p_video->p_timing);
  p_video->had_vsync_this_row = 0;
  video_set_vsync_raise_state(p_video);

  p_video->timer_fire_force_vsync_start = 0;
  p_video->timer_fire_force_vsync_end = 1;

  r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  timer_value = (p_video->vsync_pulse_width * (r0 + 1));
  timer_value *= p_video->clock_tick_multiplier;
  (void) timing_set_timer_value(p_video->p_timing,
                                p_video->timer_id,
                                timer_value);
}

static void
video_jump_to_vsync_end(struct video_struct* p_video) {
  uint32_t timer_value;
  uint32_t r0;

  assert(p_video->has_sane_framing_parameters);
  assert(p_video->had_vsync_this_row);
  assert(p_video->scanline_counter == 0);
  assert(p_video->vsync_scanline_counter == p_video->vsync_pulse_width);
  assert(p_video->vert_counter ==
         p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
  /* Jump from previous state (vsync start) to new state (vsync end). */
  p_video->scanline_counter = (p_video->vsync_pulse_width *
                               p_video->scanline_stride);
  p_video->vsync_scanline_counter = 0;
  p_video->prev_system_ticks =
      timing_get_scaled_total_timer_ticks(p_video->p_timing);
  video_set_vsync_lower_state(p_video);

  p_video->timer_fire_force_vsync_start = 1;
  p_video->timer_fire_force_vsync_end = 0;

  r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  timer_value = p_video->frame_crtc_ticks;
  timer_value -= (p_video->vsync_pulse_width * (r0 + 1));
  timer_value *= p_video->clock_tick_multiplier;
  (void) timing_set_timer_value(p_video->p_timing,
                                p_video->timer_id,
                                timer_value);
}

static void
video_timer_fired(void* p) {
  struct video_struct* p_video = (struct video_struct*) p;

  assert(!p_video->externally_clocked);

  /* Performance shortcut. If rendering is inactive and we're using
   * frame-to-frame timers, we can set the state directly instead of manually
   * running the CRTC tick by tick.
   */
  if (p_video->timer_fire_force_vsync_start) {
    assert(!p_video->is_rendering_active);
    video_jump_to_vsync_start(p_video);
  } else if (p_video->timer_fire_force_vsync_end) {
    assert(!p_video->is_rendering_active);
    video_jump_to_vsync_end(p_video);
  } else {
    video_advance_crtc_timing(p_video);
    video_update_timer(p_video);
  }

  /* If rendering is inactive, make it active again if we've hit a wall time
   * vsync. If fast mode persists, it'll go inactive immediately after the
   * next paint at the next vsync raise.
   */
  if (!p_video->is_rendering_active &&
      p_video->is_wall_time_vsync_hit &&
      video_is_at_vsync_start(p_video)) {
    struct render_struct* p_render = p_video->p_render;
    /* Wrestle the renderer to match the current odd or even interlace frame
     * state.
     */
    /* NOTE: need to call render_vsync() prior to marking rendering active
     * again, otherwise the flyback callback would attempt to paint.
     */
    render_vsync(p_render, 0);
    if (!p_video->is_interlace || p_video->is_even_interlace_frame) {
      render_hsync(p_render, 0);
    }

    p_video->is_rendering_active = 1;
    p_video->is_wall_time_vsync_hit = 0;
    p_video->timer_fire_force_vsync_start = 0;
    p_video->timer_fire_force_vsync_end = 0;
  }
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
    case 0:
      mode = k_render_mode8;
      break;
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
  video_advance_crtc_timing(p_video);

  p_video->crtc_registers[k_crtc_reg_light_pen_high] = (address_counter >> 8);
  p_video->crtc_registers[k_crtc_reg_light_pen_low] = (address_counter & 0xFF);
}

static void
video_recalculate_framing_sanity(struct video_struct* p_video) {
  uint32_t sane_r9;
  int32_t frame_crtc_ticks = -1;
  int sane = 1;

  uint32_t r0 = p_video->crtc_registers[k_crtc_reg_horiz_total];
  uint32_t r4 = p_video->crtc_registers[k_crtc_reg_vert_total];
  uint32_t r7 = p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  uint32_t r9 = p_video->crtc_registers[k_crtc_reg_lines_per_character];

  if (r0 < 3) {
    sane = 0;
  }
  /* Make sure vsync pulse width fits within one character row (which is
   * restricted to being 4 lines or more below.
   */
  assert(p_video->vsync_pulse_width != 0);
  if (p_video->vsync_pulse_width > 3) {
    sane = 0;
  }
  /* R4 > R7 > R6. */
  if (r4 <= r7) {
    sane = 0;
  }
  if (r7 <= p_video->crtc_registers[k_crtc_reg_vert_displayed]) {
    sane = 0;
  }
  sane_r9 = 3;
  if (p_video->is_interlace_sync_and_video) {
    sane_r9 *= 2;
  }
  if (r9 < sane_r9) {
    sane = 0;
  }

  if (sane) {
    uint32_t scanlines = (r9 + 1);

    if (p_video->is_interlace_sync_and_video) {
      scanlines = ((scanlines + 1) / 2);
    }
    frame_crtc_ticks = (r0 + 1);
    frame_crtc_ticks *= scanlines;
    frame_crtc_ticks *= (r4 + 1);

    frame_crtc_ticks += ((r0 + 1) *
                         p_video->crtc_registers[k_crtc_reg_vert_adjust]);
    if (p_video->is_interlace) {
      frame_crtc_ticks += ((r0 + 1) / 2);
    }
  }

  p_video->has_sane_framing_parameters = sane;
  p_video->frame_crtc_ticks = frame_crtc_ticks;
}

struct video_struct*
video_create(uint8_t* p_bbc_mem,
             uint8_t* p_shadow_mem,
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
  struct video_struct* p_video = util_mallocz(sizeof(struct video_struct));

  p_video->p_bbc_mem = p_bbc_mem;
  p_video->p_shadow_mem = p_shadow_mem;
  p_video->externally_clocked = externally_clocked;
  p_video->p_timing = p_timing;
  p_video->p_render = p_render;
  p_video->p_teletext = p_teletext;
  p_video->p_system_via = p_system_via;
  p_video->p_framebuffer_ready_callback = p_framebuffer_ready_callback;
  p_video->p_framebuffer_ready_object = p_framebuffer_ready_object;
  p_video->p_fast_flag = p_fast_flag;

  p_video->wall_time = 0;
  p_video->vsync_next_time = 0;
  p_video->num_vsyncs = 0;
  p_video->num_crtc_advances = 0;

  p_video->timer_id = timing_register_timer(p_timing,
                                            video_timer_fired,
                                            p_video);

  p_video->crtc_address_register = 0;

  p_video->frames_skip = 0;
  p_video->frame_skip_counter = 0;
  (void) util_get_u32_option(&p_video->frames_skip,
                             p_options->p_opt_flags,
                             "video:frames-skip=");
  p_video->render_every_ticks = 0;
  (void) util_get_u32_option(&p_video->render_every_ticks,
                             p_options->p_opt_flags,
                             "video:render-every-ticks=");

  if (p_system_via) {
    via_set_CB2_changed_callback(p_system_via,
                                 video_CB2_changed_callback,
                                 p_video);
  }

  if (!externally_clocked) {
    render_set_flyback_callback(p_render, video_flyback_callback, p_video);
  }

  video_init_timer(p_video);

  return p_video;
}

void
video_destroy(struct video_struct* p_video) {
  render_set_flyback_callback(p_video->p_render, NULL, NULL);
  util_free(p_video);
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
    screen_wrap_add = 0;
    assert(0);
    break;
  }

  if (p_video->screen_wrap_add == screen_wrap_add) {
    return;
  }

  /* Changing the screen wrap addition could affect rendering, so catch up. */
  if (p_video->is_rendering_active) {
    video_advance_crtc_timing(p_video);
  }

  p_video->screen_wrap_add = screen_wrap_add;
}

void
video_shadow_mode_updated(struct video_struct* p_video,
                          int is_shadow_displayed,
                          int is_shadow_paged) {
  if (p_video->is_rendering_active) {
    video_advance_crtc_timing(p_video);
  }
  p_video->is_shadow_displayed = is_shadow_displayed;
  p_video->is_shadow_paged = is_shadow_paged;
}

static void
video_ula_power_on_reset(struct video_struct* p_video) {
  /* Teletext mode, 1MHz operation. */
  p_video->video_ula_control = k_ula_teletext;
  (void) memset(&p_video->ula_palette, '\0', sizeof(p_video->ula_palette));
  p_video->screen_wrap_add = 0;
  p_video->clock_tick_multiplier = 2;
  p_video->is_shadow_displayed = 0;
  p_video->is_shadow_paged = 0;
}

static void
video_crtc_power_on_reset(struct video_struct* p_video) {
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
  p_video->crtc_registers[k_crtc_reg_sync_width] =
      (p_video->hsync_pulse_width | (p_video->vsync_pulse_width << 4));
  p_video->crtc_registers[k_crtc_reg_vert_total] = 30;
  p_video->crtc_registers[k_crtc_reg_vert_adjust] = 2;
  p_video->crtc_registers[k_crtc_reg_vert_displayed] = 25;
  /* NOTE: a real model B sets 28, even though AUG claims 27. */
  p_video->crtc_registers[k_crtc_reg_vert_sync_position] = 28;
  /* Interlace sync and video, 1 character display delay, 2 character cursor
   * delay.
   */
  p_video->crtc_registers[k_crtc_reg_interlace] = (3 | (1 << 4) | (2 << 6));
  p_video->crtc_registers[k_crtc_reg_lines_per_character] = 18;
  /* Flashing cursor, scanlines 18 - 19. */
  p_video->crtc_registers[k_crtc_reg_cursor_start] = 0x72;
  p_video->crtc_registers[k_crtc_reg_cursor_end] = 0x13;
  p_video->crtc_registers[k_crtc_reg_mem_addr_high] = 0;
  p_video->crtc_registers[k_crtc_reg_mem_addr_low] = 0;
  p_video->crtc_registers[k_crtc_reg_cursor_high] = 0;
  p_video->crtc_registers[k_crtc_reg_cursor_low] = 0;
  p_video->crtc_registers[k_crtc_reg_light_pen_high] = 0;
  p_video->crtc_registers[k_crtc_reg_light_pen_low] = 0;

  p_video->crtc_address_register = 0;

  /* Set correctly as per above register values. */
  p_video->is_interlace = 1;
  p_video->is_interlace_sync_and_video = 1;
  p_video->is_master_display_enable = 1;
  p_video->scanline_stride = 2;
  p_video->scanline_mask = 0x1E;
  p_video->hsync_pulse_width = 4;
  p_video->vsync_pulse_width = 2;
  p_video->half_r0 = 32;
  p_video->cursor_disabled = 0;
  p_video->cursor_flashing = 1;
  p_video->cursor_flash_mask = 0x10;
  p_video->cursor_start_line = 18;

  video_recalculate_framing_sanity(p_video);
  assert(p_video->has_sane_framing_parameters);

  /* CRTC non-register state. */
  p_video->crtc_frames = 0;
  p_video->is_even_interlace_frame = 1;
  p_video->is_odd_interlace_frame = 0;
  p_video->horiz_counter = 0;
  p_video->scanline_counter = 0;
  p_video->vert_counter = 0;
  p_video->vert_adjust_counter = 0;
  p_video->vsync_scanline_counter = 0;
  p_video->hsync_tick_counter = 0;
  p_video->address_counter = 0;
  p_video->address_counter_this_row = 0;
  p_video->address_counter_next_row = 0;
  p_video->in_vert_adjust = 0;
  p_video->in_vsync = 0;
  p_video->in_hsync = 0;
  p_video->in_dummy_raster = 0;
  p_video->had_vsync_this_row = 0;
  p_video->do_dummy_raster = 0;
  p_video->display_enable_horiz = 1;
  p_video->display_enable_vert = 1;
  p_video->has_hit_cursor_line_start = 0;
  p_video->has_hit_cursor_line_end = 0;
  p_video->is_end_of_main_latched = 0;
  p_video->is_end_of_frame_latched = 0;
  p_video->start_of_line_state_checks = 1;
  p_video->is_first_frame_scanline = 1;
}

void
video_power_on_reset(struct video_struct* p_video) {
  video_crtc_power_on_reset(p_video);
  video_ula_power_on_reset(p_video);

  /* Other state that needs resetting. */
  p_video->is_framing_changed_for_render = 1;
  p_video->is_wall_time_vsync_hit = 1;
  p_video->is_rendering_active = 1;
  p_video->timer_fire_force_vsync_start = 0;
  p_video->timer_fire_force_vsync_end = 0;
  p_video->frame_skip_counter = 0;
  p_video->prev_system_ticks = 0;

  /* Deliberately don't reset the counters. */

  video_mode_updated(p_video);
  video_update_timer(p_video);
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

  if (!p_video->externally_clocked) {
    p_video->is_wall_time_vsync_hit = 1;
    return;
  }

  p_system_via = p_video->p_system_via;
  via_set_CA1(p_system_via, 0);
  via_set_CA1(p_system_via, 1);

  video_do_paint(p_video);
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

  assert(p_video->externally_clocked);

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
  uint32_t hsync_pulse_ticks = (p_video->hsync_pulse_width *
                                p_video->clock_tick_multiplier);

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

  render_vsync(p_render, 0);
  teletext_VSYNC_changed(p_teletext, 0);

  for (i_lines = 0; i_lines < num_pre_lines; ++i_lines) {
    (void) render_hsync(p_render, hsync_pulse_ticks);
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
        bbc_address = video_calculate_bbc_address(crtc_line_address,
                                                  i_lines,
                                                  screen_wrap_add);
        func_render_data(p_render, p_bbc_mem[bbc_address]);
        crtc_line_address++;
      }
      (void) render_hsync(p_render, hsync_pulse_ticks);
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

static void
video_framing_changed(struct video_struct* p_video) {
  p_video->is_framing_changed_for_render = 1;
  if (p_video->externally_clocked) {
    return;
  }

  video_update_timer(p_video);
}

void
video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val) {
  uint8_t index;
  uint8_t rgbf;

  int old_flash;
  int new_flash;
  int new_clock_speed;
  int old_clock_speed;

  if (p_video->is_rendering_active) {
    video_advance_crtc_timing(p_video);
  }

  if (addr == 1) {
    /* Palette register. */
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
  old_clock_speed = video_get_clock_speed(p_video);
  new_clock_speed = !!(val & k_ula_clock_speed);

  if (new_clock_speed != old_clock_speed) {
    if (!p_video->is_rendering_active) {
      video_advance_crtc_timing(p_video);
    }
    p_video->video_ula_control = val;
    p_video->clock_tick_multiplier = 1;
    if (new_clock_speed == 0) {
      p_video->clock_tick_multiplier = 2;
    }
    video_framing_changed(p_video);
  }

  p_video->video_ula_control = val;

  new_flash = video_get_flash(p_video);
  if (old_flash != new_flash) {
    uint32_t i;
    for (i = 0; i < 16; ++i) {
      if (p_video->ula_palette[i] & 0x8) {
        video_update_real_color(p_video, i);
      }
    }
  }

  /* NOTE: yes, the last two are repeated. */
  render_set_cursor_segments(p_video->p_render,
                             !!(val & 0x80),
                             !!(val & 0x40),
                             !!(val & 0x20),
                             !!(val & 0x20));

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

static void
video_update_cursor_disabled(struct video_struct* p_video) {
  int cursor_disabled = 0;
  if ((p_video->crtc_registers[k_crtc_reg_cursor_start] & 0x60) == 0x20) {
    cursor_disabled = 1;
  }
  if ((p_video->crtc_registers[k_crtc_reg_interlace] & 0xC0) == 0xC0) {
    cursor_disabled = 1;
  }
  p_video->cursor_disabled = cursor_disabled;
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

  if (reg >= k_video_crtc_num_registers) {
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
    /* NOTE: width of 9 exempted here because some awesome tricky demos use
     * it for half-character horizontal scrolling.
     */
    if ((hsync_pulse_width != 8) &&
        (hsync_pulse_width != 4) &&
        (hsync_pulse_width != 9)) {
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

  if (p_video->is_rendering_active || !does_not_change_framing) {
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
    video_update_cursor_disabled(p_video);
    break;
  case k_crtc_reg_cursor_start:
    p_video->cursor_flashing = !!(val & 0x40);
    if (val & 0x20) {
      p_video->cursor_flash_mask = 0x10;
    } else {
      p_video->cursor_flash_mask = 0x08;
    }
    p_video->cursor_start_line = (val & 0x1F);
    video_update_cursor_disabled(p_video);
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
    video_recalculate_framing_sanity(p_video);
    video_framing_changed(p_video);
  }

  if (reg == k_crtc_reg_vert_sync_position) {
    /* A special case after the vsync position register was changed. A change of
     * this register is actually capable of triggering an immediate vsync + IRQ
     * on the Hitachi 6845, even in the middle of a scanline. So we need to
     * check for vsync hit.
     */
    video_advance_crtc_timing(p_video);
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
  for (i = 0; i < k_video_crtc_num_registers; ++i) {
    p_values[i] = p_video->crtc_registers[i];
  }
}

void
video_set_crtc_registers(struct video_struct* p_video,
                         const uint8_t* p_values) {
  uint32_t i;
  for (i = 0; i < k_video_crtc_num_registers; ++i) {
    p_video->crtc_registers[i] = p_values[i];
  }
}

void
video_get_crtc_state(struct video_struct* p_video,
                     uint8_t* p_horiz_counter,
                     uint8_t* p_scanline_counter,
                     uint8_t* p_vert_counter,
                     uint16_t* p_address_counter) {
  video_advance_crtc_timing(p_video);

  *p_horiz_counter = p_video->horiz_counter;
  *p_scanline_counter = p_video->scanline_counter;
  *p_vert_counter = p_video->vert_counter;
  *p_address_counter = (uint16_t) p_video->address_counter;
}

#include "test-video.c"
