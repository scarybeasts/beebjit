/* Appends at the end of video.c. */

#include "test.h"

enum {
  k_ticks_mode7_per_scanline = (64 * 2),
  k_ticks_mode7_per_frame = ((((31 * 10) + 2) * k_ticks_mode7_per_scanline) +
                             (k_ticks_mode7_per_scanline / 2)),
  k_ticks_mode7_to_vsync_odd = (27 * 10 * k_ticks_mode7_per_scanline),
  k_ticks_mode7_to_vsync_even = ((27 * 10 * k_ticks_mode7_per_scanline) +
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
  /* Tests simple advancement through a MODE7 frame and checks the vsync timer
   * is correctly calculated in different states.
   * MODE7 has it all: interlace, vertical adjust lines, etc.
   */
  int64_t countdown = timing_get_countdown(g_p_timing);
  /* Default should be MODE7. */
  test_expect_u32(27, g_p_video->crtc_registers[k_crtc_reg_vert_sync_position]);
  test_expect_u32(k_ticks_mode7_to_vsync_even, video_test_get_timer());
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(k_ticks_mode7_to_vsync_even, video_test_get_timer());
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_start);

  test_expect_u32(k_ticks_mode7_to_vsync_even, countdown);
  countdown--;
  (void) timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32((k_ticks_mode7_to_vsync_even - 1), video_test_get_timer());
  countdown--;
  (void) timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(1, g_p_video->horiz_counter);
  test_expect_u32((k_ticks_mode7_to_vsync_even - 2), video_test_get_timer());

  /* Move to vsync. */
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(1, g_video_test_framebuffer_ready_calls);
  test_expect_u32(32, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32(0, g_p_video->timer_fire_expect_vsync_start);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_end);
  test_expect_u32(2, g_p_video->vsync_scanline_counter);
  test_expect_u32(1, g_p_video->had_vsync_this_frame);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), video_test_get_timer());

  /* Move out of vsync. */
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(32, g_p_video->horiz_counter);
  test_expect_u32(4, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->timer_fire_expect_vsync_start);
  test_expect_u32(0, g_p_video->timer_fire_expect_vsync_end);
  test_expect_u32(0, g_p_video->vsync_scanline_counter);
  test_expect_u32((k_ticks_mode7_per_frame - (k_ticks_mode7_per_scanline * 2)),
                  video_test_get_timer());

  /* Move into vert adjust. */
  countdown -= (k_ticks_mode7_per_scanline / 2);
  countdown -= (((4 * 10) - 3) * k_ticks_mode7_per_scanline);
  countdown = timing_advance_time(g_p_timing, countdown);
  /* Force advance and timer recalculate. R0=63. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 63);
  test_expect_u32(32, g_p_video->half_r0);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(1, g_p_video->in_vert_adjust);
  test_expect_u32(0, g_p_video->vert_adjust_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32((k_ticks_mode7_to_vsync_odd +
                      (3 * k_ticks_mode7_per_scanline)),
                  video_test_get_timer());

  /* Move into dummy raster. */
  countdown -= (2 * k_ticks_mode7_per_scanline);
  countdown = timing_advance_time(g_p_timing, countdown);
  /* Force advance and timer recalculate. R0=63. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 63);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->in_vert_adjust);
  test_expect_u32(1, g_p_video->in_dummy_raster);
  test_expect_u32((k_ticks_mode7_to_vsync_odd + k_ticks_mode7_per_scanline),
                  video_test_get_timer());

  /* Move into new frame. */
  countdown -= k_ticks_mode7_per_scanline;
  countdown = timing_advance_time(g_p_timing, countdown);
  /* Force advance and timer recalculate. R0=63. */
  video_crtc_write(g_p_video, 0, 0);
  video_crtc_write(g_p_video, 1, 63);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(0, g_p_video->in_vert_adjust);
  test_expect_u32(0, g_p_video->in_dummy_raster);
  test_expect_u32(k_ticks_mode7_to_vsync_odd, video_test_get_timer());
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
  test_expect_u32(((k_ticks_mode7_to_vsync_even / 2) - 1),
                  video_test_get_timer());
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
  test_expect_u32((k_ticks_mode7_to_vsync_even - (3 * 2)),
                  video_test_get_timer());
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
  test_expect_u32(((k_ticks_mode7_to_vsync_even / 2) - 4),
                  video_test_get_timer());
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
  test_expect_u32((k_ticks_mode7_to_vsync_even - (6 * 2) - 1),
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

static void
video_test_advance_and_timing_mode4_nointerlace() {
  /* Tests for correct timing in a no-interlace mode. */
  int64_t countdown;

  /* Default settings are MODE7, so switch to something closer to MODE4. */
  test_expect_u32(30, g_p_video->crtc_registers[k_crtc_reg_vert_total]);

  video_crtc_write(g_p_video, 0, k_crtc_reg_interlace);
  video_crtc_write(g_p_video, 1, 0);
  video_crtc_write(g_p_video, 0, k_crtc_reg_lines_per_character);
  video_crtc_write(g_p_video, 1, 7);
  video_crtc_write(g_p_video, 0, k_crtc_reg_vert_adjust);
  video_crtc_write(g_p_video, 1, 0);

  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32((k_ticks_mode7_per_scanline * 8 * 27), countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), countdown);

  countdown -= (k_ticks_mode7_per_scanline * 8 * 4);
  countdown = timing_advance_time(g_p_timing, countdown);
  video_advance_crtc_timing(g_p_video);
  test_expect_u32(0, g_p_video->horiz_counter);
  test_expect_u32(0, g_p_video->vert_counter);
  test_expect_u32(0, g_p_video->scanline_counter);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 8 * 27), countdown);
}

static void
video_test_advance_and_timing_mode4_interlace() {
  /* Tests for correct timing in a non-MODE7 interlace mode. */
  int64_t countdown;

  /* Default settings are MODE7, so switch to something closer to MODE4. */
  test_expect_u32(30, g_p_video->crtc_registers[k_crtc_reg_vert_total]);

  video_crtc_write(g_p_video, 0, k_crtc_reg_interlace);
  video_crtc_write(g_p_video, 1, 1);
  video_crtc_write(g_p_video, 0, k_crtc_reg_lines_per_character);
  video_crtc_write(g_p_video, 1, 7);
  video_crtc_write(g_p_video, 0, k_crtc_reg_vert_adjust);
  video_crtc_write(g_p_video, 1, 0);

  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32(((k_ticks_mode7_per_scanline * 8 * 27) +
                      (k_ticks_mode7_per_scanline / 2)),
                  countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(((k_ticks_mode7_per_scanline * 246) +
                      (k_ticks_mode7_per_scanline / 2)),
                  countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32((k_ticks_mode7_per_scanline * 2), countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(0, g_p_video->in_vsync);
  test_expect_u32(((k_ticks_mode7_per_scanline * 246) +
                      (k_ticks_mode7_per_scanline / 2)),
                  countdown);
}

static void
video_test_additional_interlace_timing() {
  /* Test for additional interlace timing conditions not tested above. */
  test_expect_u32(1, g_p_video->is_interlace);
  test_expect_u32(1, g_p_video->is_even_interlace_frame);

  /* Simulate start of scanline where vsync will fire half-way. */
  g_p_video->is_even_interlace_frame = 0;
  g_p_video->is_odd_interlace_frame = 1;
  g_p_video->vert_counter =
      g_p_video->crtc_registers[k_crtc_reg_vert_sync_position];
  video_update_timer(g_p_video);
  test_expect_u32((k_ticks_mode7_per_scanline / 2),
                  video_test_get_timer());

  /* Simulate after R6 and before R7, even frame.
   * Note, on the even frame, post-R6 will show as the odd frame in our state
   * variables.
   */
  g_p_video->is_even_interlace_frame = 0;
  g_p_video->is_odd_interlace_frame = 1;
  g_p_video->vert_counter =
      g_p_video->crtc_registers[k_crtc_reg_vert_displayed];
  video_update_timer(g_p_video);
  test_expect_u32(((k_ticks_mode7_per_scanline * 10 * 2) +
                      (k_ticks_mode7_per_scanline / 2)),
                  video_test_get_timer());

  /* Simulate after R6 and before R7, odd frame. */
  g_p_video->is_even_interlace_frame = 1;
  g_p_video->is_odd_interlace_frame = 0;
  g_p_video->vert_counter =
      g_p_video->crtc_registers[k_crtc_reg_vert_displayed];
  video_update_timer(g_p_video);
  test_expect_u32((k_ticks_mode7_per_scanline * 10 * 2),
                  video_test_get_timer());

  /* Simulate before R6 and R7, even frame, vsync already hit. */
  g_p_video->is_even_interlace_frame = 1;
  g_p_video->is_odd_interlace_frame = 0;
  g_p_video->had_vsync_this_frame = 1;
  g_p_video->vert_counter = 0;
  video_update_timer(g_p_video);
  test_expect_u32((k_ticks_mode7_per_frame +
                      (k_ticks_mode7_per_scanline / 2) +
                      k_ticks_mode7_to_vsync_odd),
                  video_test_get_timer());

  /* Check vc > R4. */
  g_p_video->crtc_frames = 1;
  g_p_video->is_even_interlace_frame = 0;
  g_p_video->is_odd_interlace_frame = 1;
  g_p_video->had_vsync_this_frame = 1;
  g_p_video->vert_counter = 0x7F;
  video_update_timer(g_p_video);
  test_expect_u32(((k_ticks_mode7_per_scanline * 10) +
                      k_ticks_mode7_to_vsync_odd),
                  video_test_get_timer());

  /* Check vert adjust with R4 > vc. */
  g_p_video->crtc_frames = 1;
  g_p_video->is_even_interlace_frame = 0;
  g_p_video->is_odd_interlace_frame = 1;
  g_p_video->had_vsync_this_frame = 1;
  g_p_video->vert_counter = 20;
  g_p_video->in_vert_adjust = 1;
  g_p_video->vert_adjust_counter = 0;
  video_update_timer(g_p_video);
  test_expect_u32(((k_ticks_mode7_per_scanline * 3) +
                      k_ticks_mode7_to_vsync_odd),
                  video_test_get_timer());
}

static void
video_test_r6_0() {
  /* Check if R6==0 works.
   * First observed causing an assert in the Atic Atac loader.
   */
  int64_t countdown;

  g_p_video->is_even_interlace_frame = 0;
  g_p_video->is_odd_interlace_frame = 1;
  g_p_video->crtc_frames = 1;
  g_p_video->had_vsync_this_frame = 0;
  g_p_video->vert_counter = 31;
  g_p_video->in_dummy_raster = 1;
  g_p_video->crtc_registers[k_crtc_reg_vert_displayed] = 0;
  video_update_timer(g_p_video);
  test_expect_u32((k_ticks_mode7_per_scanline + k_ticks_mode7_to_vsync_odd),
                  video_test_get_timer());
  countdown = timing_get_countdown(g_p_timing);
  test_expect_u32((k_ticks_mode7_per_scanline + k_ticks_mode7_to_vsync_odd),
                  countdown);
  countdown = 0;
  countdown = timing_advance_time(g_p_timing, countdown);
  test_expect_u32(1, g_p_video->in_vsync);
  test_expect_u32(1, g_p_video->is_even_interlace_frame);
  test_expect_u32(0, g_p_video->is_odd_interlace_frame);
}


void
video_test() {
  video_test_init();
  video_test_advance_and_timing_mode7();
  video_test_end();

  video_test_init();
  video_test_clock_speed_flip();
  video_test_end();

  video_test_init();
  video_test_write_vs_advance();
  video_test_end();

  video_test_init();
  video_test_advance_and_timing_mode4_nointerlace();
  video_test_end();

  video_test_init();
  video_test_advance_and_timing_mode4_interlace();
  video_test_end();

  video_test_init();
  video_test_additional_interlace_timing();
  video_test_end();

  video_test_init();
  video_test_r6_0();
  video_test_end();
}
