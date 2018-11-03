#include "asm_x64.h"

#include "util.h"

void
asm_x64_copy(struct util_buffer* p_buf, void* p_start, void* p_end) {
  size_t size = (p_end - p_start);
  util_buffer_add_chunk(p_buf, p_start, size);
}

void
asm_x64_emit_instruction_CRASH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_CRASH, asm_x64_instruction_CRASH_END);
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
asm_x64_emit_instruction_TSX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TSX, asm_x64_instruction_TSX_END);
}

void
asm_x64_emit_instruction_TXS(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_instruction_TXS, asm_x64_instruction_TXS_END);
}

void
asm_x64_emit_instruction_BCC_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BCC_interp,
               asm_x64_instruction_BCC_interp_END);
}

void
asm_x64_emit_instruction_BCS_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BCS_interp,
               asm_x64_instruction_BCS_interp_END);
}

void
asm_x64_emit_instruction_BEQ_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BEQ_interp,
               asm_x64_instruction_BEQ_interp_END);
}

void
asm_x64_emit_instruction_BMI_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BMI_interp,
               asm_x64_instruction_BMI_interp_END);
}

void
asm_x64_emit_instruction_BNE_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BNE_interp,
               asm_x64_instruction_BNE_interp_END);
}

void
asm_x64_emit_instruction_BPL_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BPL_interp,
               asm_x64_instruction_BPL_interp_END);
}

void
asm_x64_emit_instruction_BRK_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_pc_plus_1_to_scratch,
               asm_x64_inturbo_pc_plus_1_to_scratch_END);
  asm_x64_emit_push_word_from_scratch(p_buf);
  asm_x64_emit_instruction_PHP(p_buf);
  asm_x64_copy(p_buf,
               asm_x64_inturbo_interrupt_vector,
               asm_x64_inturbo_interrupt_vector_END);
}

void
asm_x64_emit_instruction_BVC_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BVC_interp,
               asm_x64_instruction_BVC_interp_END);
}

void
asm_x64_emit_instruction_BVS_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BVS_interp,
               asm_x64_instruction_BVS_interp_END);
}

void
asm_x64_emit_instruction_ADC_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ADC_imm_interp,
               asm_x64_instruction_ADC_imm_interp_END);
}

void
asm_x64_emit_instruction_ADC_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ADC_scratch_interp,
               asm_x64_instruction_ADC_scratch_interp_END);
}

void
asm_x64_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CMP_imm_interp,
               asm_x64_instruction_CMP_imm_interp_END);
}

void
asm_x64_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CMP_scratch_interp,
               asm_x64_instruction_CMP_scratch_interp_END);
}

void
asm_x64_emit_instruction_CPX_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CPX_imm_interp,
               asm_x64_instruction_CPX_imm_interp_END);
}

void
asm_x64_emit_instruction_CPX_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CPX_scratch_interp,
               asm_x64_instruction_CPX_scratch_interp_END);
}

void
asm_x64_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_JMP_scratch_interp,
               asm_x64_instruction_JMP_scratch_interp_END);
}

void
asm_x64_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDA_imm_interp,
               asm_x64_instruction_LDA_imm_interp_END);
}

void
asm_x64_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDA_scratch_interp,
               asm_x64_instruction_LDA_scratch_interp_END);
}

void
asm_x64_emit_instruction_LDX_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDX_imm_interp,
               asm_x64_instruction_LDX_imm_interp_END);
}

void
asm_x64_emit_instruction_LDX_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDX_scratch_interp,
               asm_x64_instruction_LDX_scratch_interp_END);
}

void
asm_x64_emit_instruction_ROR_acc_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ROR_acc_interp,
               asm_x64_instruction_ROR_acc_interp_END);
}

void
asm_x64_emit_instruction_ROR_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ROR_scratch_interp,
               asm_x64_instruction_ROR_scratch_interp_END);
}

void
asm_x64_emit_instruction_SBC_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_SBC_imm_interp,
               asm_x64_instruction_SBC_imm_interp_END);
}

void
asm_x64_emit_instruction_SBC_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_SBC_scratch_interp,
               asm_x64_instruction_SBC_scratch_interp_END);
}

void
asm_x64_emit_instruction_STA_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_STA_scratch_interp,
               asm_x64_instruction_STA_scratch_interp_END);
}

void
asm_x64_emit_instruction_STX_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_STX_scratch_interp,
               asm_x64_instruction_STX_scratch_interp_END);
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
asm_x64_emit_inturbo_next_opcode(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_next_opcode,
               asm_x64_inturbo_next_opcode_END);
}
