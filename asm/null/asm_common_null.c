#include "../asm_common.h"

uint32_t
asm_enter(void* p_context,
          uint32_t jump_addr_x64,
          int64_t countdown,
          void* p_mem_base) {
  (void) p_context;
  (void) jump_addr_x64;
  (void) countdown;
  (void) p_mem_base;
  return 0;
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
  (void) p_buf;
}

void
asm_emit_instruction_DEY(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_INX(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_instruction_INY(struct util_buffer* p_buf) {
  (void) p_buf;
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
  (void) p_buf;
}

void
asm_emit_instruction_TAY(struct util_buffer* p_buf) {
  (void) p_buf;
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
  (void) p_buf;
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
