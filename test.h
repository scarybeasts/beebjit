#ifndef BEEBJIT_TEST_H
#define BEEBJIT_TEST_H

#include <stdint.h>

struct bbc_struct;

void test_do_tests(struct bbc_struct* p_bbc);

void test_expect_u32(uint32_t expectation, uint32_t actual);

#endif /* BEEBJIT_TEST_H */
