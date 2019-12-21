#include "test.h"

#include "bbc.h"

#include <err.h>

extern void timing_test();
extern void video_test();

void
test_do_tests(struct bbc_struct* p_bbc) {
  bbc_power_on_reset(p_bbc);
  bbc_power_on_reset(p_bbc);

  timing_test();
  video_test();
}

void
test_expect_u32(uint32_t expectation, uint32_t actual) {
  if (actual != expectation) {
    errx(1, "FAIL: %u, expected %u", actual, expectation);
  }
}
