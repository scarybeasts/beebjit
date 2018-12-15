#ifndef BEEBJIT_TEST_HELPER_H
#define BEEBJIT_TEST_HELPER_H

#include <stdint.h>

struct util_buffer;

void emit_REQUIRE_ZF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_NF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_CF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_OF(struct util_buffer* p_buf, int require);

void emit_REQUIRE_EQ(struct util_buffer* p_buf, uint8_t val);

#endif /* BEEBJIT_TEST_HELPER_H */
