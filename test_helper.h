#ifndef BEEBJIT_TEST_HELPER_H
#define BEEBJIT_TEST_HELPER_H

struct util_buffer;

void emit_REQUIRE_ZF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_NF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_CF(struct util_buffer* p_buf, int require);
void emit_REQUIRE_OF(struct util_buffer* p_buf, int require);

#endif /* BEEBJIT_TEST_HELPER_H */
