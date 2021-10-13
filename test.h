#ifndef BEEBJIT_TEST_H
#define BEEBJIT_TEST_H

#include <stddef.h>
#include <stdint.h>

struct bbc_struct;

void test_do_tests(struct bbc_struct* p_bbc);

void test_expect_u32(uint32_t expectation, uint32_t actual);
void test_expect_eq(uint32_t v1, uint32_t v2);
void test_expect_neq(uint32_t v1, uint32_t v2);
void test_expect_binary(uint8_t* p_expect, uint8_t* p_actual, size_t len);

#endif /* BEEBJIT_TEST_H */
