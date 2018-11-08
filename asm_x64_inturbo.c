#include "asm_x64_inturbo.h"

#include "asm_x64_common.h"
#include "util.h"

void
asm_x64_emit_inturbo_advance_pc_1(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_advance_pc_1,
               asm_x64_inturbo_advance_pc_1_END);
}

void
asm_x64_emit_inturbo_advance_pc_2(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_advance_pc_2,
               asm_x64_inturbo_advance_pc_2_END);
}

void
asm_x64_emit_inturbo_advance_pc_3(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_advance_pc_3,
               asm_x64_inturbo_advance_pc_3_END);
}

void
asm_x64_emit_inturbo_next_opcode(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_next_opcode,
               asm_x64_inturbo_next_opcode_END);
}

void
asm_x64_emit_inturbo_enter_debug(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_enter_debug,
               asm_x64_inturbo_enter_debug_END);
}

void
asm_x64_emit_inturbo_mode_zpg(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_zpg,
               asm_x64_inturbo_mode_zpg_END);
}

void
asm_x64_emit_inturbo_mode_abs(struct util_buffer* p_buf,
                              uint16_t special_addr_above) {
  int lea_patch = (0x10000 - special_addr_above);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_abs,
               asm_x64_inturbo_mode_abs_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_inturbo_mode_abs,
                    asm_x64_inturbo_mode_abs_lea_patch,
                    lea_patch);
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_inturbo_mode_abs,
                     asm_x64_inturbo_mode_abs_jb_patch,
                     asm_x64_inturbo_special_addr);
}

void
asm_x64_emit_inturbo_mode_abx(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_abx,
               asm_x64_inturbo_mode_abx_END);
}

void
asm_x64_emit_inturbo_mode_aby(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_aby,
               asm_x64_inturbo_mode_aby_END);
}

void
asm_x64_emit_inturbo_mode_zpx(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_zpx,
               asm_x64_inturbo_mode_zpx_END);
}

void
asm_x64_emit_inturbo_mode_zpy(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_zpy,
               asm_x64_inturbo_mode_zpy_END);
}

void
asm_x64_emit_inturbo_mode_idx(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_idx,
               asm_x64_inturbo_mode_idx_END);
}

void
asm_x64_emit_inturbo_mode_idy(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_mode_idy,
               asm_x64_inturbo_mode_idy_END);
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
asm_x64_emit_instruction_BIT_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_BIT_interp,
               asm_x64_instruction_BIT_interp_END);
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
               asm_x64_inturbo_pc_plus_2_to_scratch,
               asm_x64_inturbo_pc_plus_2_to_scratch_END);
  asm_x64_emit_push_word_from_scratch(p_buf);
  asm_x64_emit_instruction_PHP(p_buf);
  asm_x64_emit_instruction_SEI(p_buf);
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
asm_x64_emit_instruction_AND_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_AND_imm_interp,
               asm_x64_instruction_AND_imm_interp_END);
}

void
asm_x64_emit_instruction_AND_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_AND_scratch_interp,
               asm_x64_instruction_AND_scratch_interp_END);
}

void
asm_x64_emit_instruction_ASL_acc_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ASL_acc_interp,
               asm_x64_instruction_ASL_acc_interp_END);
}

void
asm_x64_emit_instruction_ASL_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_ASL_scratch_interp,
               asm_x64_instruction_ASL_scratch_interp_END);
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
asm_x64_emit_instruction_INC_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_INC_scratch_interp,
               asm_x64_instruction_INC_scratch_interp_END);
}

void
asm_x64_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_JMP_scratch_interp,
               asm_x64_instruction_JMP_scratch_interp_END);
}

void
asm_x64_emit_instruction_JSR_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_inturbo_pc_plus_2_to_scratch,
               asm_x64_inturbo_pc_plus_2_to_scratch_END);
  asm_x64_emit_push_word_from_scratch(p_buf);
  asm_x64_copy(p_buf,
               asm_x64_inturbo_load_pc_from_pc,
               asm_x64_inturbo_load_pc_from_pc_END);
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
asm_x64_emit_instruction_LDY_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDY_imm_interp,
               asm_x64_instruction_LDY_imm_interp_END);
}

void
asm_x64_emit_instruction_LDY_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDY_scratch_interp,
               asm_x64_instruction_LDY_scratch_interp_END);
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
asm_x64_emit_instruction_RTI_interp(struct util_buffer* p_buf) {
  asm_x64_emit_instruction_PLP(p_buf);
  asm_x64_emit_pull_word_to_scratch(p_buf);
  asm_x64_emit_instruction_JMP_scratch_interp(p_buf);
}

void
asm_x64_emit_instruction_RTS_interp(struct util_buffer* p_buf) {
  asm_x64_emit_pull_word_to_scratch(p_buf);
  asm_x64_copy(p_buf,
               asm_x64_inturbo_JMP_scratch_plus_1_interp,
               asm_x64_inturbo_JMP_scratch_plus_1_interp_END);
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
