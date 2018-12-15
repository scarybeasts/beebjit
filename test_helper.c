#include "test_helper.h"

#include "emit_6502.h"

void
emit_REQUIRE_ZF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BEQ(p_buf, 1);
  } else {
    emit_BNE(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_NF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BMI(p_buf, 1);
  } else {
    emit_BPL(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_CF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BCS(p_buf, 1);
  } else {
    emit_BCC(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

void
emit_REQUIRE_OF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BVS(p_buf, 1);
  } else {
    emit_BVC(p_buf, 1);
  }
  emit_CRASH(p_buf);
}
