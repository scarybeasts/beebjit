/* Appends at the end of video.c. */

#include "test.h"

enum {
  k_ticks_mode7_per_scanline = (64 * 2),
  k_ticks_mode7_per_frame = ((((31 * 10) + 2) * k_ticks_mode7_per_scanline) +
                             (k_ticks_mode7_per_scanline / 2)),
  k_ticks_mode7_to_vsync_odd = (28 * 10 * k_ticks_mode7_per_scanline),
  k_ticks_mode7_to_vsync_even = ((28 * 10 * k_ticks_mode7_per_scanline) +
                                 (k_ticks_mode7_per_scanline / 2)),
};

struct bbc_options g_p_options;
uint8_t* g_p_bbc_mem = NULL;
struct timing_struct* g_p_timing = NULL;
struct teletext_struct* g_p_teletext = NULL;
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
  g_p_teletext = teletext_create();
  g_p_render = render_create(g_p_teletext, &g_p_options);
  g_p_video = video_create(g_p_bbc_mem,
                           0,
                           g_p_timing,
                           g_p_render,
                           g_p_teletext,
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
  teletext_destroy(g_p_teletext);
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
video_test_advance_and_timing_mode7() {
  /* Tests simple advancement through a MODE7 frame and checks the vsync state
   * is correct.
   * MODE7 has it all: interlace, vertical adjust lines, etc.
   */
  int64_t countdown = timing_get_countdown(g_p_timing);
  video_advance_crtc_timing(g_p_video);
  /* Default should be MODE7. */
  test_expect_u32(28, g_p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
  test_expect_u32(1, g_p_video->is_interlace);
  test_expect_u32(1, g_p_video->is_interlace_sync_and_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(0, g_p_video->crtc_frames);
  test_expect_u32(1, g_p_video->is_even_interlace_frame);
  test_expect_u32(0, g_p_video->is_odd_interlace_frame);

  countdown = timing_advance_time(g_p_timing,
                                  (countdown - k_ticks_mode7_to_vsync_odd));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->crtc_frames);
  test_expect_u32(0, g_p_video->is_even_interlace_frame);
  test_expect_u32(1, g_p_video->is_odd_interlace_frame);
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline / 2)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(32, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vsync);
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline * 1.5)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vsync);
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline / 2)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->in_vsync);

  /* Move to start of next frame. */
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline / 2)));
  video_advance_crtc_timing(g_p_video);
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline * (7 + (10 * 2) + 2 + 1))));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->vert_counter);

  countdown = timing_advance_time(g_p_timing,
                                  (countdown - k_ticks_mode7_to_vsync_odd));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vsync);
  countdown = timing_advance_time(
      g_p_timing,
      (countdown - (k_ticks_mode7_per_scanline * 2)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vsync);
}

static void
video_test_clock_speed_flip() {
  /* Tests switching from 1MHz to 2MHz 6845 CRTC operation and visa versa. This
   * is a tricky operation!
   */
  int64_t countdown = timing_get_countdown(g_p_timing);

  test_expect_u32(0, g_p_video->horiz_counter);
  /* We default to MODE7; should be 1MHz. */
  test_expect_u32(0, video_get_clock_speed(g_p_video));

  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->horiz_counter);

  /* 1->2MHz, at even cycle. */
  video_ula_write(g_p_video, 0, 0x12);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32(1, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(2, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(3, g_p_video->horiz_counter);

  /* 2->1MHz, at even cycle. */
  video_ula_write(g_p_video, 0, 0x02);
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32(3, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(3, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(4, g_p_video->horiz_counter);

  /* 1->2MHz, at odd cycle. */
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(4, g_p_video->horiz_counter);
  test_expect_u32(1, (timing_get_total_timer_ticks(g_p_timing) & 1));
  video_ula_write(g_p_video, 0, 0x12);
  countdown = timing_get_countdown(g_p_timing);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(5, g_p_video->horiz_counter);

  /* 2->1MHz, at odd cycle. */
  countdown = timing_advance_time(g_p_timing, (countdown- 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(6, g_p_video->horiz_counter);
  test_expect_u32(1, (timing_get_total_timer_ticks(g_p_timing) & 1));
  video_ula_write(g_p_video, 0, 0x02);
  countdown = timing_get_countdown(g_p_timing);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(7, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(7, g_p_video->horiz_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 1));
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

static void
video_test_full_frame_timers() {
  /* Tests that with sane CRTC parameters, vsync on -> off -> on uses single
   * timers for the full length.
   */
  int64_t countdown = timing_get_countdown(g_p_timing);

  /* Default should be MODE7. */
  test_expect_u32(28, g_p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
  countdown = timing_advance_time(g_p_timing,
                                  (countdown - k_ticks_mode7_to_vsync_even));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), video_test_get_timer());
  countdown = timing_advance_time(g_p_timing,
                                  (countdown - video_test_get_timer()));
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_frame - (k_ticks_mode7_per_scanline * 2)),
                  video_test_get_timer());
  countdown = timing_advance_time(g_p_timing,
                                  (countdown - video_test_get_timer()));
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), video_test_get_timer());
  countdown = timing_advance_time(g_p_timing,
                                  (countdown - video_test_get_timer()));
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_frame - (k_ticks_mode7_per_scanline * 2)),
                  video_test_get_timer());
  countdown = timing_advance_time(g_p_timing,
                                  (countdown - video_test_get_timer()));
  test_expect_u32(1, g_p_video->in_vsync);

  /* Now back where we started: vsync raise of even frame. */
  /* Do some checks that non-frame-changing register writes don't reload the
   * timer.
   */
  countdown = timing_advance_time(g_p_timing, (countdown - 10));
  test_expect_u32(((k_ticks_mode7_per_scanline * 2) - 10),
                  video_test_get_timer());
  /* ULA palette. */
  video_ula_write(g_p_video, 1, 0xF0);
  test_expect_u32(((k_ticks_mode7_per_scanline * 2) - 10),
                  video_test_get_timer());
  /* ULA control: flash and pixels per character. Making sure to keep the clock
   * rate at 1MHz.
   */
  video_ula_write(g_p_video, 0, 0x00);
  video_ula_write(g_p_video, 0, 0xE5);
  test_expect_u32(((k_ticks_mode7_per_scanline * 2) - 10),
                  video_test_get_timer());
  /* CRTC start address. */
  video_crtc_write(g_p_video, 0, 13);
  video_crtc_write(g_p_video, 1, 0xAA);
  test_expect_u32(((k_ticks_mode7_per_scanline * 2) - 10),
                  video_test_get_timer());
  /* Cursor address. */
  video_crtc_write(g_p_video, 0, 15);
  video_crtc_write(g_p_video, 1, 0xAA);
  test_expect_u32(((k_ticks_mode7_per_scanline * 2) - 10),
                  video_test_get_timer());

  /* Change the framing and check the timer changed. */
  /* Vertical total. */
  video_crtc_write(g_p_video, 0, 4);
  video_crtc_write(g_p_video, 1, 8);
  test_expect_u32(54, video_test_get_timer());
}

static void
video_test_out_of_frame() {
  /* Tests situations where the counters are outside of the defined frame. */
  int64_t countdown = timing_get_countdown(g_p_timing);
  countdown = timing_advance_time(g_p_timing, (countdown - 20));
  /* Horizontal total to 7, while horiz_counter == 10. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 7);
  /* Timer should restore sanity by bringing horiz_counter back to 0. */
  test_expect_u32((246 * 2), video_test_get_timer());
}

static void
video_test_6845_corner_cases() {
  /* Tests corner cases, expecting Hitachi 6845 behavior. */
  int64_t countdown = timing_get_countdown(g_p_timing);

  /* Interlace off to keep it simpler. */
  video_crtc_write(g_p_video, 0, 8);
  video_crtc_write(g_p_video, 1, 0);
  video_crtc_write(g_p_video, 0, 9);
  video_crtc_write(g_p_video, 1, 9);

  /* R7=0 should vsync ok. */
  video_crtc_write(g_p_video, 0, 7);
  video_crtc_write(g_p_video, 1, 0);
  /* Vsync pulse width 0 should be 16. */
  video_crtc_write(g_p_video, 0, 3);
  video_crtc_write(g_p_video, 1, 0x04);
  /* Turning off interlace changed countdown. */
  countdown = timing_get_countdown(g_p_timing);

  countdown = timing_advance_time(g_p_timing, (countdown - (312 * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(1, g_p_video->in_vsync);

  countdown = timing_advance_time(g_p_timing, (countdown - (15 * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->in_vsync);
  countdown = timing_advance_time(g_p_timing, (countdown - (1 * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->in_vsync);

  /* R7=R4+1 should vsync ok if there is vert adjust. */
  video_crtc_write(g_p_video, 0, 7);
  video_crtc_write(g_p_video, 1, 31);

  countdown = timing_advance_time(g_p_timing, (countdown - ((312 - 16) * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(0, g_p_video->in_vsync);

  countdown = timing_advance_time(g_p_timing, (countdown - (310 * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->in_vert_adjust);
  test_expect_u32(1, g_p_video->in_vsync);

  /* MA should increment outside the display area. */
  countdown = timing_advance_time(g_p_timing, (countdown - (2 * 128)));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->display_enable_horiz);
  test_expect_u32(0, g_p_video->address_counter);
  countdown = timing_advance_time(g_p_timing, (countdown - 100));
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(50, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->display_enable_horiz);
  test_expect_u32(50, g_p_video->address_counter);
}

void
video_test() {
  video_test_init();
  video_test_advance_and_timing_mode7();
  video_test_end();

  video_test_init();
  video_test_write_vs_advance();
  video_test_end();

  video_test_init();
  video_test_clock_speed_flip();
  video_test_end();

  video_test_init();
  video_test_full_frame_timers();
  video_test_end();

  video_test_init();
  video_test_out_of_frame();
  video_test_end();

  video_test_init();
  video_test_6845_corner_cases();
  video_test_end();
}
