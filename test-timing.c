/* Appends at the end of timing.c. */

#include "test.h"

static void
timing_test_timer_fired(void* p) {
  (void) p;
}

static void
timing_test_basics() {
  uint64_t countdown;

  struct timing_struct* p_timing = timing_create(1);

  uint32_t t1 = timing_register_timer(p_timing, timing_test_timer_fired, NULL);
  uint32_t t2 = timing_register_timer(p_timing, timing_test_timer_fired, NULL);

  test_expect_u32(0, timing_get_total_timer_ticks(p_timing));
  test_expect_u32(0, timing_get_scaled_total_timer_ticks(p_timing));

  test_expect_u32(0, timing_timer_is_running(p_timing, t1));

  countdown = timing_start_timer_with_value(p_timing, t1, 100);
  test_expect_u32(100, countdown);
  test_expect_u32(100, timing_get_timer_value(p_timing, t1));

  countdown = timing_set_timer_value(p_timing, t2, 50);
  test_expect_u32(100, countdown);
  test_expect_u32(50, timing_get_timer_value(p_timing, t2));

  timing_destroy(p_timing);
}

void
timing_test() {
  timing_test_basics();
}
