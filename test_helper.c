#include "test_helper.h"

#include "defs_6502.h"
#include "emit_6502.h"

void
emit_REQUIRE_ZF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BEQ(p_buf, k_emit_crash_len);
  } else {
    emit_BNE(p_buf, k_emit_crash_len);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_NF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BMI(p_buf, k_emit_crash_len);
  } else {
    emit_BPL(p_buf, k_emit_crash_len);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_CF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BCS(p_buf, k_emit_crash_len);
  } else {
    emit_BCC(p_buf, k_emit_crash_len);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_OF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BVS(p_buf, k_emit_crash_len);
  } else {
    emit_BVC(p_buf, k_emit_crash_len);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_EQ(struct util_buffer* p_buf, uint8_t val) {
  emit_CMP(p_buf, k_imm, val);
  emit_REQUIRE_ZF(p_buf, 1);
}
