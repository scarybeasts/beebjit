#include "../asm_common.h"

#include "../../util.h"
#include "asm_helper_arm64.h"

#include <assert.h>
/* For ssize_t. */
#include <sys/types.h>

void
asm_copy(struct util_buffer* p_buf, void* p_start, void* p_end) {
  size_t size = (p_end - p_start);
  util_buffer_add_chunk(p_buf, p_start, size);
}

void
asm_fill_with_trap(struct util_buffer* p_buf) {
  static uint8_t s_brk0[4] = { 0x00, 0x00, 0x20, 0xd4 };
  size_t pos = util_buffer_get_pos(p_buf);
  size_t length = (util_buffer_get_length(p_buf) - pos);
  assert((pos % 4) == 0);
  while (length) {
    util_buffer_add_chunk(p_buf, &s_brk0[0], 4);
    length -= 4;
  }
}

/* ARM64 helpers. */
void
asm_patch_arm64_imm12(struct util_buffer* p_buf, uint32_t val) {
  uint8_t* p_raw;
  size_t pos;
  uint32_t insn;
  uint32_t* p_insn;
  assert(val <= 4095);

  p_raw = util_buffer_get_ptr(p_buf);
  pos = util_buffer_get_pos(p_buf);
  assert(pos >= 4);
  pos -= 4;
  p_raw += pos;
  p_insn = (uint32_t*) p_raw;
  insn = *p_insn;
  insn &= ~(0xFFF << 10);
  insn |= ((val & 0xFFF) << 10);
  *p_insn = insn;
}

void
asm_copy_patch_arm64_imm12(struct util_buffer* p_buf,
                           void* p_start,
                           void* p_end,
                           uint32_t val) {
  asm_copy(p_buf, p_start, p_end);
  asm_patch_arm64_imm12(p_buf, val);
}

void
asm_patch_arm64_imm16(struct util_buffer* p_buf, uint32_t val) {
  uint8_t* p_raw;
  size_t pos;
  uint32_t insn;
  uint32_t* p_insn;
  assert(val <= 0xFFFF);

  p_raw = util_buffer_get_ptr(p_buf);
  pos = util_buffer_get_pos(p_buf);
  assert(pos >= 4);
  pos -= 4;
  p_raw += pos;
  p_insn = (uint32_t*) p_raw;
  insn = *p_insn;
  insn &= ~(0xFFFF << 5);
  insn |= ((val & 0xFFFF) << 5);
  *p_insn = insn;
}

void
asm_copy_patch_arm64_imm16(struct util_buffer* p_buf,
                           void* p_start,
                           void* p_end,
                           uint32_t val) {
  asm_copy(p_buf, p_start, p_end);
  asm_patch_arm64_imm16(p_buf, val);
}

static void
asm_patch_arm64_immN_pc_rel(struct util_buffer* p_buf,
                            void* p_target,
                            uint32_t bit_count,
                            uint32_t shift) {
  uint8_t* p_raw;
  uint8_t* p_src;
  ssize_t pos;
  uint32_t insn;
  uint32_t* p_insn;
  intptr_t delta;
  uint32_t mask;
  int32_t range;

  mask = ((1 << bit_count) - 1);
  range = (1 << (bit_count - 1));

  (void) range;

  p_raw = util_buffer_get_ptr(p_buf);
  pos = util_buffer_get_pos(p_buf);
  assert(pos >= 4);
  pos -= 4;
  p_raw += pos;

  p_src = util_buffer_get_base_address(p_buf);
  p_src += pos;
  delta = ((uint8_t*) p_target - p_src);
  delta /= 4;
  assert((delta <= (range - 1)) && (delta >= -range));

  p_insn = (uint32_t*) p_raw;
  insn = *p_insn;
  insn &= ~(mask << shift);
  insn |= ((delta & mask) << shift);
  *p_insn = insn;
}

void
asm_patch_arm64_imm14_pc_rel(struct util_buffer* p_buf, void* p_target) {
  asm_patch_arm64_immN_pc_rel(p_buf, p_target, 14, 5);
}

void
asm_copy_patch_arm64_imm14_pc_rel(struct util_buffer* p_buf,
                                  void* p_start,
                                  void* p_end,
                                  void* p_target) {
  asm_copy(p_buf, p_start, p_end);
  asm_patch_arm64_imm14_pc_rel(p_buf, p_target);
}

void
asm_patch_arm64_imm19_pc_rel(struct util_buffer* p_buf, void* p_target) {
  asm_patch_arm64_immN_pc_rel(p_buf, p_target, 19, 5);
}

void
asm_copy_patch_arm64_imm19_pc_rel(struct util_buffer* p_buf,
                                  void* p_start,
                                  void* p_end,
                                  void* p_target) {
  asm_copy(p_buf, p_start, p_end);
  asm_patch_arm64_imm19_pc_rel(p_buf, p_target);
}

void
asm_patch_arm64_imm26_pc_rel(struct util_buffer* p_buf, void* p_target) {
  asm_patch_arm64_immN_pc_rel(p_buf, p_target, 26, 0);
}

void
asm_copy_patch_arm64_imm26_pc_rel(struct util_buffer* p_buf,
                                  void* p_start,
                                  void* p_end,
                                  void* p_target) {
  asm_copy(p_buf, p_start, p_end);
  asm_patch_arm64_imm26_pc_rel(p_buf, p_target);
}

/* Instructions. */
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
  void asm_instruction_CLV(void);
  void asm_instruction_CLV_END(void);
  asm_copy(p_buf, asm_instruction_CLV, asm_instruction_CLV_END);
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
  void asm_instruction_TXA(void);
  void asm_instruction_TXA_END(void);
  asm_copy(p_buf, asm_instruction_TXA, asm_instruction_TXA_END);
}

void
asm_emit_instruction_TXS(struct util_buffer* p_buf) {
  void asm_instruction_TXS(void);
  void asm_instruction_TXS_END(void);
  asm_copy(p_buf, asm_instruction_TXS, asm_instruction_TXS_END);
}

void
asm_emit_instruction_TYA(struct util_buffer* p_buf) {
  void asm_instruction_TYA(void);
  void asm_instruction_TYA_END(void);
  asm_copy(p_buf, asm_instruction_TYA, asm_instruction_TYA_END);
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
