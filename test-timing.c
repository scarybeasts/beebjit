/* Appends at the end of timing.c. */

#include "test.h"

static uint32_t g_timing_test_timer_hits = 0;

static void
timing_test_timer_fired(void* p) {
  uint32_t i;

  struct timing_struct* p_timing = (struct timing_struct*) p;

  g_timing_test_timer_hits++;

  for (i = 0; i < p_timing->max_timer; ++i) {
    if (p_timing->firing[i] && (p_timing->timings[i] == 0)) {
      timing_stop_timer(p_timing, i);
    }
  }
}

static void
timing_test_basics() {
  uint64_t countdown;

  struct timing_struct* p_timing = timing_create(1);

  uint32_t t1 = timing_register_timer(p_timing,
                                      timing_test_timer_fired,
                                      p_timing);
  uint32_t t2 = timing_register_timer(p_timing,
                                      timing_test_timer_fired,
                                      p_timing);
  uint32_t t3 = timing_register_timer(p_timing,
                                      timing_test_timer_fired,
                                      p_timing);

  test_expect_u32(0, timing_get_total_timer_ticks(p_timing));
  test_expect_u32(0, timing_get_scaled_total_timer_ticks(p_timing));

  test_expect_u32(0, timing_timer_is_running(p_timing, t1));

  countdown = timing_start_timer_with_value(p_timing, t1, 100);
  test_expect_u32(1, timing_timer_is_running(p_timing, t1));
  test_expect_u32(1, timing_get_firing(p_timing, t1));
  test_expect_u32(100, countdown);
  test_expect_u32(100, timing_get_timer_value(p_timing, t1));

  countdown = timing_set_timer_value(p_timing, t2, 50);
  test_expect_u32(0, timing_timer_is_running(p_timing, t2));
  test_expect_u32(100, countdown);
  test_expect_u32(50, timing_get_timer_value(p_timing, t2));
  countdown = timing_start_timer(p_timing, t2);
  test_expect_u32(50, countdown);
  countdown = timing_set_firing(p_timing, t2, 0);
  test_expect_u32(100, countdown);

  (void) timing_set_timer_value(p_timing, t3, 200);

  countdown -= 1;
  countdown = timing_advance_time(p_timing, countdown);
  test_expect_u32(99, countdown);
  test_expect_u32(99, timing_get_timer_value(p_timing, t1));
  test_expect_u32(49, timing_get_timer_value(p_timing, t2));
  test_expect_u32(200, timing_get_timer_value(p_timing, t3));

  countdown -= 99;
  countdown = timing_advance_time(p_timing, countdown);
  test_expect_u32(1, g_timing_test_timer_hits);
  test_expect_u32(0, timing_get_timer_value(p_timing, t1));
  test_expect_u32((uint32_t) -50,
                  (uint32_t) timing_get_timer_value(p_timing, t2));

  timing_destroy(p_timing);
}

void
timing_test() {
  timing_test_basics();
}
