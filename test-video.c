/* Appends at the end of video.c. */

#include "test.h"

enum {
  k_ticks_mode7_per_scanline = (64 * 2),
  k_ticks_mode7_per_frame = (((31 * 10) + 2) * k_ticks_mode7_per_scanline),
  k_ticks_mode7_to_vsync = (27 * 10 * k_ticks_mode7_per_scanline),
};

struct bbc_options g_p_options;
uint8_t* g_p_bbc_mem = NULL;
struct timing_struct* g_p_timing = NULL;
struct render_struct* g_p_render = NULL;
struct video_struct* g_p_video = NULL;
uint32_t g_video_test_framebuffer_ready_calls = 0;
int g_test_fast_flag = 0;

static void
video_test_framebuffer_ready_callback(void* p,
                                      int do_full_paint,
                                      int framing_changed) {
  (void) p;
  (void) do_full_paint;
  (void) framing_changed;

  g_video_test_framebuffer_ready_calls++;
}

static void
video_test_init() {
  g_p_options.p_opt_flags = "";
  g_p_options.p_log_flags = "";
  g_p_options.accurate = 1;
  g_p_bbc_mem = malloc(0x10000);
  g_p_timing = timing_create(1);
  g_p_render = render_create(&g_p_options);
  g_p_video = video_create(g_p_bbc_mem,
                           0,
                           g_p_timing,
                           g_p_render,
                           NULL,
                           NULL,
                           video_test_framebuffer_ready_callback,
                           NULL,
                           &g_test_fast_flag,
                           &g_p_options);
}

static void
video_test_end() {
  video_destroy(g_p_video);
  render_destroy(g_p_render);
  timing_destroy(g_p_timing);
  free(g_p_bbc_mem);
  g_p_video = NULL;
  g_p_render = NULL;
  g_p_timing = NULL;
  g_p_bbc_mem = NULL;
}

static uint32_t
video_test_get_timer() {
  uint32_t timer_id = g_p_video->video_timer_id;
  return (uint32_t) timing_get_timer_value(g_p_timing, timer_id);
}

static void
video_test_advance_and_timing() {
  /* Tests simple advancement through a MODE7 frame and checks the vsync timer
   * is correctly calculated in different states.
   */
  int64_t countdown = timing_get_countdown(g_p_timing);
  /* Default should be MODE7. */
  test_expect_u32(27, g_p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
  test_expect_u32(k_ticks_mode7_to_vsync, video_test_get_timer());
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(k_ticks_mode7_to_vsync, video_test_get_timer());
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_start);

  test_expect_u32(k_ticks_mode7_to_vsync, countdown);
  countdown--;
  (void) timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32((k_ticks_mode7_to_vsync - 1), video_test_get_timer());
  countdown--;
  (void) timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->horiz_counter);
  test_expect_u32((k_ticks_mode7_to_vsync - 2), video_test_get_timer());

  /* Move to vsync. */
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_video_test_framebuffer_ready_calls);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32(0, g_p_video->timer_fire_expect_vsync_start);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_end);
  test_expect_u32(2, g_p_video->vsync_scanline_counter);
  test_expect_u32(1, g_p_video->had_vsync_this_frame);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), video_test_get_timer());

  /* Move out of vsync. */
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(4, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_start);
  test_expect_u32(0, g_p_video->timer_fire_expect_vsync_end);
  test_expect_u32(0, g_p_video->vsync_scanline_counter);
  test_expect_u32((k_ticks_mode7_per_frame - (k_ticks_mode7_per_scanline * 2)),
                  video_test_get_timer());

  /* Move into vert adjust. */
  countdown -= (((4 * 10) - 2) * k_ticks_mode7_per_scanline);
  countdown = timing_advance_time(g_p_timing, countdown);
  /* Force advance and timer recalculate. R0=63. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 63);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vert_adjust);
  test_expect_u32(0, g_p_video->vert_adjust_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32((((27 * 10) + 2) * k_ticks_mode7_per_scanline),
                  video_test_get_timer());

  /* Move into new frame. */
  countdown -= (2 * k_ticks_mode7_per_scanline);
  countdown = timing_advance_time(g_p_timing, countdown);
  /* Force advance and timer recalculate. R0=63. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 63);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(0, g_p_video->in_vert_adjust);
  test_expect_u32(((27 * 10) * k_ticks_mode7_per_scanline),
                  video_test_get_timer());
}

static void
video_test_clock_speed_flip() {
  /* Tests switching from 1MHz to 2MHz 6845 CRTC operation and visa versa. This
   * is a tricky operation!
   */
  int64_t countdown = timing_get_countdown(g_p_timing);

  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  /* We default to MODE7; should be 1MHz. */
  test_expect_u32(0, video_get_clock_speed(g_p_video));

  /* 1->2MHz, at even cycle. */
  countdown -= 2;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_ula_write(g_p_video, 0, 0x12);
  test_expect_u32(1, g_p_video->horiz_counter);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32(((k_ticks_mode7_to_vsync / 2) - 1), video_test_get_timer());
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(2, g_p_video->horiz_counter);
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(3, g_p_video->horiz_counter);

  /* 2->1MHz, at even cycle. */
  video_ula_write(g_p_video, 0, 0x02);
  test_expect_u32(3, g_p_video->horiz_counter);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32((k_ticks_mode7_to_vsync - (3 * 2)), video_test_get_timer());
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(3, g_p_video->horiz_counter);
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(4, g_p_video->horiz_counter);

  /* 1->2MHz, at odd cycle. */
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(4, g_p_video->horiz_counter);
  test_expect_u32(1, (timing_get_total_timer_ticks(g_p_timing) & 1));
  video_ula_write(g_p_video, 0, 0x12);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32(((k_ticks_mode7_to_vsync / 2) - 4), video_test_get_timer());
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(5, g_p_video->horiz_counter);

  /* 2->1MHz, at odd cycle. */
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(6, g_p_video->horiz_counter);
  test_expect_u32(1, (timing_get_total_timer_ticks(g_p_timing) & 1));
  video_ula_write(g_p_video, 0, 0x02);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32((k_ticks_mode7_to_vsync - (6 * 2) - 1),
                  video_test_get_timer());
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(7, g_p_video->horiz_counter);
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(7, g_p_video->horiz_counter);
  countdown -= 1;
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(8, g_p_video->horiz_counter);
}

static void
video_test_write_vs_advance() {
  /* Tests that when doing a CRTC write, the advance occurs with pre-write
   * CRTC state.
   */
  int64_t countdown = timing_get_countdown(g_p_timing);

  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(1, g_p_video->is_interlace_sync_and_video);

  /* Advance one scanline's worth of time. Should increment 6845 scanline
   * counter by 2 in interlace sync and video mode.
   */
  countdown -= 128;
  countdown = timing_advance_time(g_p_timing, countdown);

  video_crtc_write(g_p_video, 0, k_crtc_reg_interlace);
  video_crtc_write(g_p_video, 1, 0);
  test_expect_u32(0, g_p_video->is_interlace_sync_and_video);

  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(2, g_p_video->scanline_counter);
}

void
video_test() {
  video_test_init();
  video_test_advance_and_timing();
  video_test_end();

  video_test_init();
  video_test_clock_speed_flip();
  video_test_end();

  video_test_init();
  video_test_write_vs_advance();
  video_test_end();
}
