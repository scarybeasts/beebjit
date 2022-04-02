#include "test.h"

#include "bbc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void timing_test(void);
extern void video_test(void);
extern void jit_test(struct bbc_struct* p_bbc);
extern void expression_test(void);
extern void bbc_test(struct bbc_struct* p_bbc);

void
test_do_tests(struct bbc_struct* p_bbc) {
  bbc_power_on_reset(p_bbc);
  bbc_power_on_reset(p_bbc);

  timing_test();
  video_test();
  jit_test(p_bbc);
  expression_test();
  bbc_test(p_bbc);
  (void) printf("Tests OK!\n");
}

void
test_expect_u32(uint32_t expectation, uint32_t actual) {
  if (actual != expectation) {
    (void) fprintf(stderr, "FAIL: %u, expected %u\n", actual, expectation);
    assert(0);
    exit(1);
  }
}

void
test_expect_eq(uint32_t v1, uint32_t v2) {
  if (v1 != v2) {
    (void) fprintf(stderr, "FAIL: should be equal %u %u\n", v1, v2);
    assert(0);
    exit(1);
  }
}

void
test_expect_neq(uint32_t v1, uint32_t v2) {
  if (v1 == v2) {
    (void) fprintf(stderr, "FAIL: should not be equal %u %u\n", v1, v2);
    assert(0);
    exit(1);
  }
}

void
test_expect_binary(uint8_t* p_expect, uint8_t* p_actual, size_t len) {
  int ret = memcmp(p_expect, p_actual, len);
  if (ret != 0) {
    (void) fprintf(stderr, "FAIL: binary expectation\n");
    assert(0);
    exit(1);
  }
}
