#include "asm_x64_common.h"

#include "util.h"

#include <assert.h>
#include <limits.h>

void
asm_x64_copy(struct util_buffer* p_buf, void* p_start, void* p_end) {
  size_t size = (p_end - p_start);
  util_buffer_add_chunk(p_buf, p_start, size);
}

void
asm_x64_patch_int(struct util_buffer* p_buf,
                  size_t offset,
                  void* p_start,
                  void* p_patch,
                  int value) {
  size_t original_pos = util_buffer_get_pos(p_buf);
  ssize_t pos = (offset + (p_patch - p_start));

  assert(pos >= (ssize_t) sizeof(int));

  util_buffer_set_pos(p_buf, (pos - 4));
  util_buffer_add_int(p_buf, value);
  util_buffer_set_pos(p_buf, original_pos);
}

void
asm_x64_patch_jump(struct util_buffer* p_buf,
                   size_t offset,
                   void* p_start,
                   void* p_patch,
                   void* p_jump_target) {
  size_t original_pos = util_buffer_get_pos(p_buf);
  ssize_t pos = (offset + (p_patch - p_start));
  void* p_jump_pc = (util_buffer_get_ptr(p_buf) + pos);
  ssize_t jump_delta = (p_jump_target - p_jump_pc);

  assert(pos >= (ssize_t) sizeof(int));
  assert(jump_delta <= INT_MAX);
  assert(jump_delta >= INT_MIN);

  util_buffer_set_pos(p_buf, (pos - 4));
  util_buffer_add_int(p_buf, (int) jump_delta);
  util_buffer_set_pos(p_buf, original_pos);
}

void
asm_x64_emit_instruction_CRASH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_CRASH, asm_x64_instruction_CRASH_END);
}

void
asm_x64_emit_instruction_EXIT(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_EXIT, asm_x64_instruction_EXIT_END);
}

void
asm_x64_emit_instruction_REAL_NOP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_REAL_NOP,
               asm_x64_instruction_REAL_NOP_END);
}

void
asm_x64_emit_instruction_TRAP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TRAP, asm_x64_instruction_TRAP_END);
}

void
asm_x64_emit_instruction_CLC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_CLC, asm_x64_instruction_CLC_END);
}

void
asm_x64_emit_instruction_CLD(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_CLD, asm_x64_instruction_CLD_END);
}

void
asm_x64_emit_instruction_CLI(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_CLI, asm_x64_instruction_CLI_END);
}

void
asm_x64_emit_instruction_DEX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_DEX, asm_x64_instruction_DEX_END);
}

void
asm_x64_emit_instruction_DEY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_DEY, asm_x64_instruction_DEY_END);
}

void
asm_x64_emit_instruction_INX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_INX, asm_x64_instruction_INX_END);
}

void
asm_x64_emit_instruction_INY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_INY, asm_x64_instruction_INY_END);
}

void
asm_x64_emit_instruction_PHA(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_PHA, asm_x64_instruction_PHA_END);
}

void
asm_x64_emit_instruction_PHP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_asm_emit_intel_flags_to_scratch,
               asm_x64_asm_emit_intel_flags_to_scratch_END);
  asm_x64_copy(p_buf,
               asm_x64_set_brk_flag_in_scratch,
               asm_x64_set_brk_flag_in_scratch_END);
  asm_x64_copy(p_buf, asm_x64_push_from_scratch, asm_x64_push_from_scratch_END);
}

void
asm_x64_emit_instruction_PLA(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_PLA, asm_x64_instruction_PLA_END);
}

void
asm_x64_emit_instruction_PLP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_pull_to_scratch, asm_x64_pull_to_scratch_END);
  asm_x64_copy(p_buf,
               asm_x64_asm_set_intel_flags_from_scratch,
               asm_x64_asm_set_intel_flags_from_scratch_END);
}

void
asm_x64_emit_instruction_SEC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_SEC, asm_x64_instruction_SEC_END);
}

void
asm_x64_emit_instruction_SEI(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_SEI, asm_x64_instruction_SEI_END);
}

void
asm_x64_emit_instruction_TAX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TAX, asm_x64_instruction_TAX_END);
}

void
asm_x64_emit_instruction_TAY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TAY, asm_x64_instruction_TAY_END);
}

void
asm_x64_emit_instruction_TSX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TSX, asm_x64_instruction_TSX_END);
}

void
asm_x64_emit_instruction_TXS(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TXS, asm_x64_instruction_TXS_END);
}

void
asm_x64_emit_instruction_A_NZ_flags(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_A_NZ_flags,
               asm_x64_instruction_A_NZ_flags_END);
}

void
asm_x64_emit_instruction_X_NZ_flags(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_X_NZ_flags,
               asm_x64_instruction_X_NZ_flags_END);
}

void
asm_x64_emit_instruction_Y_NZ_flags(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_Y_NZ_flags,
               asm_x64_instruction_Y_NZ_flags_END);
}

void
asm_x64_emit_push_word_from_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_push_word_from_scratch,
               asm_x64_push_word_from_scratch_END);
}

void
asm_x64_emit_pull_word_to_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_pull_word_to_scratch,
               asm_x64_pull_word_to_scratch_END);
}
