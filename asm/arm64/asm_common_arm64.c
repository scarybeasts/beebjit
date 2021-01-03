#include "../asm_common.h"

#include "../../util.h"

void
asm_copy(struct util_buffer* p_buf, void* p_start, void* p_end) {
  size_t size = (p_end - p_start);
  util_buffer_add_chunk(p_buf, p_start, size);
}

void
asm_emit_instruction_CRASH(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_REAL_NOP(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_TRAP(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_ILLEGAL(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_BIT_common(struct util_buffer* p_buf) {
  void asm_instruction_BIT_common(void);
  void asm_instruction_BIT_common_END(void);
  asm_copy(p_buf, asm_instruction_BIT_common, asm_instruction_BIT_common_END);
}

void
asm_emit_instruction_CLC(struct util_buffer* p_buf) {
  void asm_instruction_CLC(void);
  void asm_instruction_CLC_END(void);
  asm_copy(p_buf, asm_instruction_CLC, asm_instruction_CLC_END);
}

void
asm_emit_instruction_CLD(struct util_buffer* p_buf) {
  void asm_instruction_CLD(void);
  void asm_instruction_CLD_END(void);
  asm_copy(p_buf, asm_instruction_CLD, asm_instruction_CLD_END);
}

void
asm_emit_instruction_CLI(struct util_buffer* p_buf) {
  void asm_instruction_CLI(void);
  void asm_instruction_CLI_END(void);
  asm_copy(p_buf, asm_instruction_CLI, asm_instruction_CLI_END);
}

void
asm_emit_instruction_CLV(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_DEX(struct util_buffer* p_buf) {
  void asm_instruction_DEX(void);
  void asm_instruction_DEX_END(void);
  asm_copy(p_buf, asm_instruction_DEX, asm_instruction_DEX_END);
}

void
asm_emit_instruction_DEY(struct util_buffer* p_buf) {
  void asm_instruction_DEY(void);
  void asm_instruction_DEY_END(void);
  asm_copy(p_buf, asm_instruction_DEY, asm_instruction_DEY_END);
}

void
asm_emit_instruction_INX(struct util_buffer* p_buf) {
  void asm_instruction_INX(void);
  void asm_instruction_INX_END(void);
  asm_copy(p_buf, asm_instruction_INX, asm_instruction_INX_END);
}

void
asm_emit_instruction_INY(struct util_buffer* p_buf) {
  void asm_instruction_INY(void);
  void asm_instruction_INY_END(void);
  asm_copy(p_buf, asm_instruction_INY, asm_instruction_INY_END);
}

void
asm_emit_instruction_PHA(struct util_buffer* p_buf) {
  void asm_instruction_PHA(void);
  void asm_instruction_PHA_END(void);
  asm_copy(p_buf, asm_instruction_PHA, asm_instruction_PHA_END);
}

void
asm_emit_instruction_PHP(struct util_buffer* p_buf) {
  void asm_emit_arm64_flags_to_scratch(void);
  void asm_emit_arm64_flags_to_scratch_END(void);
  void asm_set_brk_flag_in_scratch(void);
  void asm_set_brk_flag_in_scratch_END(void);
  void asm_push_from_scratch(void);
  void asm_push_from_scratch_END(void);
  asm_copy(p_buf,
           asm_emit_arm64_flags_to_scratch,
           asm_emit_arm64_flags_to_scratch_END);
  asm_copy(p_buf,
           asm_set_brk_flag_in_scratch,
           asm_set_brk_flag_in_scratch_END);
  asm_copy(p_buf, asm_push_from_scratch, asm_push_from_scratch_END);
}

void
asm_emit_instruction_PLA(struct util_buffer* p_buf) {
  void asm_instruction_PLA(void);
  void asm_instruction_PLA_END(void);
  asm_copy(p_buf, asm_instruction_PLA, asm_instruction_PLA_END);
}

void
asm_emit_instruction_PLP(struct util_buffer* p_buf) {
  void asm_pull_to_scratch(void);
  void asm_pull_to_scratch_END(void);
  void asm_set_arm64_flags_from_scratch(void);
  void asm_set_arm64_flags_from_scratch_END(void);
  asm_copy(p_buf, asm_pull_to_scratch, asm_pull_to_scratch_END);
  asm_copy(p_buf,
           asm_set_arm64_flags_from_scratch,
           asm_set_arm64_flags_from_scratch_END);
}

void
asm_emit_instruction_SEC(struct util_buffer* p_buf) {
  void asm_instruction_SEC(void);
  void asm_instruction_SEC_END(void);
  asm_copy(p_buf, asm_instruction_SEC, asm_instruction_SEC_END);
}

void
asm_emit_instruction_SED(struct util_buffer* p_buf) {
  void asm_instruction_SED(void);
  void asm_instruction_SED_END(void);
  asm_copy(p_buf, asm_instruction_SED, asm_instruction_SED_END);
}

void
asm_emit_instruction_SEI(struct util_buffer* p_buf) {
  void asm_instruction_SEI(void);
  void asm_instruction_SEI_END(void);
  asm_copy(p_buf, asm_instruction_SEI, asm_instruction_SEI_END);
}

void
asm_emit_instruction_TAX(struct util_buffer* p_buf) {
  void asm_instruction_TAX(void);
  void asm_instruction_TAX_END(void);
  asm_copy(p_buf, asm_instruction_TAX, asm_instruction_TAX_END);
}

void
asm_emit_instruction_TAY(struct util_buffer* p_buf) {
  void asm_instruction_TAY(void);
  void asm_instruction_TAY_END(void);
  asm_copy(p_buf, asm_instruction_TAY, asm_instruction_TAY_END);
}

void
asm_emit_instruction_TSX(struct util_buffer* p_buf) {
  void asm_instruction_TSX(void);
  void asm_instruction_TSX_END(void);
  asm_copy(p_buf, asm_instruction_TSX, asm_instruction_TSX_END);
}

void
asm_emit_instruction_TXA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_TXS(struct util_buffer* p_buf) {
  void asm_instruction_TXS(void);
  void asm_instruction_TXS_END(void);
  asm_copy(p_buf, asm_instruction_TXS, asm_instruction_TXS_END);
}

void
asm_emit_instruction_TYA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_A_NZ_flags(struct util_buffer* p_buf) {
  void asm_instruction_A_NZ_flags(void);
  void asm_instruction_A_NZ_flags_END(void);
  asm_copy(p_buf, asm_instruction_A_NZ_flags, asm_instruction_A_NZ_flags_END);
}

void
asm_emit_instruction_X_NZ_flags(struct util_buffer* p_buf) {
  void asm_instruction_X_NZ_flags(void);
  void asm_instruction_X_NZ_flags_END(void);
  asm_copy(p_buf, asm_instruction_X_NZ_flags, asm_instruction_X_NZ_flags_END);
}

void
asm_emit_instruction_Y_NZ_flags(struct util_buffer* p_buf) {
  void asm_instruction_Y_NZ_flags(void);
  void asm_instruction_Y_NZ_flags_END(void);
  asm_copy(p_buf, asm_instruction_Y_NZ_flags, asm_instruction_Y_NZ_flags_END);
}

void
asm_emit_push_word_from_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_pull_word_to_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_pull_word_to_scratch, asm_pull_word_to_scratch_END);
}
