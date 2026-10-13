#ifndef BEEBJIT_VIDEO_H
#define BEEBJIT_VIDEO_H

#include <stddef.h>
#include <stdint.h>

struct video_struct;

struct bbc_options;
struct render_struct;
struct teletext_struct;
struct timing_struct;
struct via_struct;

struct video_struct* video_create(uint8_t* p_mem,
                                  uint8_t* p_shadow_mem,
                                  int externally_clocked,
                                  struct timing_struct* p_timing,
                                  struct render_struct* p_render,
                                  struct teletext_struct* p_teletext,
                                  struct via_struct* p_system_via,
                                  void (*p_framebuffer_ready_callback)
                                      (void* p,
                                       int do_full_paint,
                                       int framing_changed,
                                       int do_wait_for_paint),
                                  void* p_framebuffer_ready_object,
                                  int* p_fast_flag,
                                  struct bbc_options* p_options);
void video_destroy(struct video_struct* p_video);

void video_set_nula(struct video_struct* p_video, int is_nula);

void video_advance_for_memory_sync(void* p);
void video_advance_crtc_timing(struct video_struct* p_video);
void video_force_paint(struct video_struct* p_video, int do_clear_after_paint);

void video_IC32_updated(struct video_struct* p_video, uint8_t IC32);
void video_mem_mode_updated(struct video_struct* p_video,
                            uint8_t romsel,
                            uint8_t acccon);

void video_power_on_reset(struct video_struct* p_video);

uint64_t video_get_num_vsyncs(struct video_struct* p_video);
uint64_t video_get_num_crtc_advances(struct video_struct* p_video);
struct render_struct* video_get_render(struct video_struct* p_video);

void video_apply_wall_time_delta(struct video_struct* p_video, uint64_t delta);

void video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val);
uint64_t video_ula_write_ctrl_with_countdown(struct video_struct* p_video,
                                             uint8_t addr,
                                             uint8_t val,
                                             uint64_t countdown);
void video_ula_write_palette_with_countdown(struct video_struct* p_video,
                                            uint8_t addr,
                                            uint8_t val,
                                            uint64_t countdown);

uint8_t video_crtc_read(struct video_struct* p_video, uint8_t addr);
void video_crtc_write(struct video_struct* p_video, uint8_t addr, uint8_t val);
void video_crtc_write_address(struct video_struct* p_video,
                              uint8_t addr,
                              uint8_t val);
uint64_t video_crtc_write_value_with_countdown(struct video_struct* p_video,
                                               uint8_t addr,
                                               uint8_t val,
                                               uint64_t countdown);

void video_render_full_frame(struct video_struct* p_video);

uint8_t video_get_ula_control(struct video_struct* p_video);
void video_set_ula_control(struct video_struct* p_video, uint8_t val);
void video_get_ula_full_palette(struct video_struct* p_video,
                                uint8_t* p_values);
void video_set_ula_palette(struct video_struct* p_video, uint8_t val);
void video_set_ula_full_palette(struct video_struct* p_video,
                                const uint8_t* p_values);

enum {
  k_video_crtc_num_registers = 18,
};
void video_get_crtc_registers(struct video_struct* p_video,
                              uint8_t* p_values);
void video_set_crtc_registers(struct video_struct* p_video,
                              const uint8_t* p_values);

void video_get_crtc_state(struct video_struct* p_video,
                          uint8_t* p_horiz_counter,
                          uint8_t* p_scanline_counter,
                          uint8_t* p_vert_counter,
                          uint16_t* p_address_counter,
                          uint64_t* p_crtc_frames,
                          int* p_is_in_vert_adjust,
                          int* p_is_in_dummy_raster);

/* The 6845 state besides its registers, to carry on exactly after a state
 * save and load. It is stored in state files as is, so a change to it needs
 * a new state version.
 */
struct video_state {
  uint64_t crtc_frames;
  int64_t last_vsync_raise_ticks;
  int64_t last_vsync_lower_ticks;
  uint32_t address_counter;
  uint32_t address_counter_saved;
  uint32_t display_enable_bits;
  uint32_t start_of_line_state_checks;
  int32_t cursor_skew_counter;
  int32_t via_ca1_irq_level;
  uint8_t crtc_address_register;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint8_t vert_adjust_counter;
  uint8_t vsync_scanline_counter;
  uint8_t hsync_tick_counter;
  uint8_t is_odd_frame;
  uint8_t is_vert_adjust_pending;
  uint8_t is_in_vert_adjust;
  uint8_t in_vsync;
  uint8_t is_even_vsync;
  uint8_t is_odd_vsync;
  uint8_t in_hsync;
  uint8_t in_dummy_raster;
  uint8_t had_odd_vsync_this_row;
  uint8_t had_even_vsync_this_row;
  uint8_t has_hit_cursor_line_start;
  uint8_t has_hit_cursor_line_end;
  uint8_t is_end_of_main_latched;
  uint8_t is_end_of_vert_adjust_latched;
  uint8_t is_end_of_frame_latched;
  uint8_t is_first_frame_scanline;
  uint8_t dispen_shifts[3];
};

void video_get_state(struct video_struct* p_video, struct video_state* p_state);
/* Also reschedules the next video event, from the restored state. */
void video_set_state(struct video_struct* p_video,
                     const struct video_state* p_state);

#endif /* BEEBJIT_VIDEO_H */
