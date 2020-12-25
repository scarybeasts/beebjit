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
  (void) p_buf;
}

void
asm_emit_instruction_CLC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_CLD(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_CLI(struct util_buffer* p_buf) {
  (void) p_buf;
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
  (void) p_buf;
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
  (void) p_buf;
}

void
asm_emit_instruction_PHP(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_PLA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_PLP(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_SEC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_SED(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_SEI(struct util_buffer* p_buf) {
  (void) p_buf;
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
  (void) p_buf;
}

void
asm_emit_instruction_TXA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_TXS(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_TYA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_A_NZ_flags(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_X_NZ_flags(struct util_buffer* p_buf) {
  void asm_instruction_X_NZ_flags(void);
  void asm_instruction_X_NZ_flags_END(void);
  asm_copy(p_buf, asm_instruction_X_NZ_flags, asm_instruction_X_NZ_flags_END);
}

void
asm_emit_instruction_Y_NZ_flags(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_push_word_from_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_pull_word_to_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}
