#include "../asm_inturbo.h"

#include "../asm_common.h"
#include "../../defs_6502.h"
#include "../../util.h"

static void
asm_emit_instruction_Bxx_interp_accurate(
    struct util_buffer* p_buf,
    void* p_Bxx_interp_accurate,
    void* p_Bxx_interp_accurate_END,
    void* p_Bxx_interp_accurate_jump_patch) {
  uint8_t* p_jump_target;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, p_Bxx_interp_accurate, p_Bxx_interp_accurate_END);

  p_jump_target = util_buffer_get_base_address(p_buf);
  p_jump_target += util_buffer_get_pos(p_buf);
  p_jump_target += (asm_instruction_Bxx_interp_accurate_not_taken_target -
                    asm_instruction_Bxx_interp_accurate);

  asm_patch_jump(p_buf,
                 offset,
                 p_Bxx_interp_accurate,
                 p_Bxx_interp_accurate_jump_patch,
                 p_jump_target);

  asm_copy(p_buf,
           asm_instruction_Bxx_interp_accurate,
           asm_instruction_Bxx_interp_accurate_END);
}

int
asm_inturbo_is_enabled(void) {
  return 1;
}

void
asm_emit_inturbo_save_countdown(struct util_buffer* p_buf) {
  void asm_inturbo_save_countdown(void);
  void asm_inturbo_save_countdown_END(void);
  asm_copy(p_buf, asm_inturbo_save_countdown, asm_inturbo_save_countdown_END);
}

void
asm_emit_inturbo_epilog(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_inturbo_check_special_address(struct util_buffer* p_buf,
                                       uint16_t special_addr_above) {
  int lea_patch = (k_6502_addr_space_size - special_addr_above);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_inturbo_check_special_address,
           asm_inturbo_check_special_address_END);
  asm_patch_int(p_buf,
                offset,
                asm_inturbo_check_special_address,
                asm_inturbo_check_special_address_lea_patch,
                lea_patch);
  /* NOTE: could consider implementing very simple load / store abs mode to
   * special registers inline. Calling out to the interpreter is expensive.
   */
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_check_special_address,
                 asm_inturbo_check_special_address_jb_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_check_countdown(struct util_buffer* p_buf, uint8_t opcycles) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_inturbo_check_countdown, asm_inturbo_check_countdown_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_inturbo_check_countdown,
                 asm_inturbo_check_countdown_lea_patch,
                 (0x100 - opcycles));
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_check_countdown,
                 asm_inturbo_check_countdown_jb_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_commit_branch(struct util_buffer* p_buf) {
  void asm_inturbo_commit_branch(void);
  void asm_inturbo_commit_branch_END(void);
  asm_copy(p_buf, asm_inturbo_commit_branch, asm_inturbo_commit_branch_END);
}

void
asm_emit_inturbo_check_decimal(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_inturbo_check_decimal, asm_inturbo_check_decimal_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_check_decimal,
                 asm_inturbo_check_decimal_jb_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_check_interrupt(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_inturbo_check_interrupt,
           asm_inturbo_check_interrupt_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_check_interrupt,
                 asm_inturbo_check_interrupt_jae_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_advance_pc_and_next(struct util_buffer* p_buf,
                                     uint8_t advance) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_inturbo_load_opcode, asm_inturbo_load_opcode_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_inturbo_load_opcode,
                 asm_inturbo_load_opcode_mov_patch,
                 advance);

  if (advance) {
    offset = util_buffer_get_pos(p_buf);
    asm_copy(p_buf, asm_inturbo_advance_pc, asm_inturbo_advance_pc_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_inturbo_advance_pc,
                   asm_inturbo_advance_pc_lea_patch,
                   advance);
  }

  asm_copy(p_buf, asm_inturbo_jump_opcode, asm_inturbo_jump_opcode_END);
}

void
asm_emit_inturbo_advance_pc_and_ret(struct util_buffer* p_buf,
                                    uint8_t advance) {
  void asm_inturbo_ret(void);
  void asm_inturbo_ret_END(void);

  if (advance) {
    size_t offset = util_buffer_get_pos(p_buf);
    asm_copy(p_buf, asm_inturbo_advance_pc, asm_inturbo_advance_pc_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_inturbo_advance_pc,
                   asm_inturbo_advance_pc_lea_patch,
                   advance);
  }

  asm_copy(p_buf, asm_inturbo_ret, asm_inturbo_ret_END);
}

void
asm_emit_inturbo_enter_debug(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  void asm_inturbo_enter_debug(void);
  void asm_inturbo_enter_debug_END(void);
  void asm_inturbo_enter_debug_call_patch(void);

  asm_copy(p_buf, asm_inturbo_enter_debug, asm_inturbo_enter_debug_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_enter_debug,
                 asm_inturbo_enter_debug_call_patch,
                 asm_debug);
}

void
asm_emit_inturbo_call_interp(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_inturbo_jump_call_interp,
           asm_inturbo_jump_call_interp_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_jump_call_interp,
                 asm_inturbo_jump_call_interp_jmp_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_do_write_invalidation(struct util_buffer* p_buf) {
  void asm_inturbo_do_write_invalidation(void);
  void asm_inturbo_do_write_invalidation_END(void);
  asm_copy(p_buf,
           asm_inturbo_do_write_invalidation,
           asm_inturbo_do_write_invalidation_END);
}

void
asm_emit_inturbo_mode_rel(struct util_buffer* p_buf) {
  void asm_inturbo_mode_rel(void);
  void asm_inturbo_mode_rel_END(void);
  asm_copy(p_buf, asm_inturbo_mode_rel, asm_inturbo_mode_rel_END);
}

void
asm_emit_inturbo_mode_zpg(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_zpg, asm_inturbo_mode_zpg_END);
}

void
asm_emit_inturbo_mode_abs(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_abs, asm_inturbo_mode_abs_END);
}

void
asm_emit_inturbo_mode_abx(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_abx, asm_inturbo_mode_abx_END);
}

void
asm_emit_inturbo_mode_abx_check_page_crossing(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_inturbo_mode_abx_check_page_crossing,
           asm_inturbo_mode_abx_check_page_crossing_END);
}

void
asm_emit_inturbo_mode_aby(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_aby, asm_inturbo_mode_aby_END);
}

void
asm_emit_inturbo_mode_aby_check_page_crossing(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_inturbo_mode_aby_check_page_crossing,
           asm_inturbo_mode_aby_check_page_crossing_END);
}

void
asm_emit_inturbo_mode_zpx(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_zpx, asm_inturbo_mode_zpx_END);
}

void
asm_emit_inturbo_mode_zpy(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_zpy, asm_inturbo_mode_zpy_END);
}

void
asm_emit_inturbo_mode_idx(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_inturbo_mode_idx, asm_inturbo_mode_idx_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_mode_idx,
                 asm_inturbo_mode_idx_jump_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_mode_idy(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_inturbo_mode_idy, asm_inturbo_mode_idy_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_inturbo_mode_idy,
                 asm_inturbo_mode_idy_jump_patch,
                 asm_inturbo_call_interp);
}

void
asm_emit_inturbo_mode_idy_check_page_crossing(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_inturbo_mode_idy_check_page_crossing,
           asm_inturbo_mode_idy_check_page_crossing_END);
}

void
asm_emit_inturbo_mode_ind(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_inturbo_mode_ind, asm_inturbo_mode_ind_END);
}

void
asm_emit_instruction_BCC_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BCC_interp, asm_instruction_BCC_interp_END);
}

void
asm_emit_instruction_BCC_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BCC_interp_accurate,
      asm_instruction_BCC_interp_accurate_END,
      asm_instruction_BCC_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BCS_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BCS_interp, asm_instruction_BCS_interp_END);
}

void
asm_emit_instruction_BCS_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BCS_interp_accurate,
      asm_instruction_BCS_interp_accurate_END,
      asm_instruction_BCS_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BEQ_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BEQ_interp, asm_instruction_BEQ_interp_END);
}

void
asm_emit_instruction_BEQ_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BEQ_interp_accurate,
      asm_instruction_BEQ_interp_accurate_END,
      asm_instruction_BEQ_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BIT_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BIT_interp, asm_instruction_BIT_interp_END);
  asm_copy(p_buf, asm_instruction_BIT_common, asm_instruction_BIT_common_END);
}

void
asm_emit_instruction_BMI_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BMI_interp, asm_instruction_BMI_interp_END);
}

void
asm_emit_instruction_BMI_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BMI_interp_accurate,
      asm_instruction_BMI_interp_accurate_END,
      asm_instruction_BMI_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BNE_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BNE_interp, asm_instruction_BNE_interp_END);
}

void
asm_emit_instruction_BNE_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BNE_interp_accurate,
      asm_instruction_BNE_interp_accurate_END,
      asm_instruction_BNE_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BPL_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BPL_interp, asm_instruction_BPL_interp_END);
}

void
asm_emit_instruction_BPL_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BPL_interp_accurate,
      asm_instruction_BPL_interp_accurate_END,
      asm_instruction_BPL_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BRK_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_inturbo_pc_plus_2_to_scratch,
           asm_inturbo_pc_plus_2_to_scratch_END);
  asm_emit_push_word_from_scratch(p_buf);
  asm_emit_instruction_PHP(p_buf);
  asm_emit_instruction_SEI(p_buf);
  asm_copy(p_buf,
           asm_inturbo_interrupt_vector,
           asm_inturbo_interrupt_vector_END);
}

void
asm_emit_instruction_BVC_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BVC_interp, asm_instruction_BVC_interp_END);
}

void
asm_emit_instruction_BVC_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BVC_interp_accurate,
      asm_instruction_BVC_interp_accurate_END,
      asm_instruction_BVC_interp_accurate_jump_patch);
}

void
asm_emit_instruction_BVS_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_instruction_BVS_interp, asm_instruction_BVS_interp_END);
}

void
asm_emit_instruction_BVS_interp_accurate(struct util_buffer* p_buf) {
  asm_emit_instruction_Bxx_interp_accurate(
      p_buf,
      asm_instruction_BVS_interp_accurate,
      asm_instruction_BVS_interp_accurate_END,
      asm_instruction_BVS_interp_accurate_jump_patch);
}

void
asm_emit_instruction_ADC_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ADC_imm_interp,
           asm_instruction_ADC_imm_interp_END);
}

void
asm_emit_instruction_ADC_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ADC_scratch_interp,
           asm_instruction_ADC_scratch_interp_END);
}

void
asm_emit_instruction_ALR_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ALR_imm_interp,
           asm_instruction_ALR_imm_interp_END);
}

void
asm_emit_instruction_AND_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_AND_imm_interp,
           asm_instruction_AND_imm_interp_END);
}

void
asm_emit_instruction_AND_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_AND_scratch_interp,
           asm_instruction_AND_scratch_interp_END);
}

void
asm_emit_instruction_ASL_acc_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ASL_acc_interp,
           asm_instruction_ASL_acc_interp_END);
}

void
asm_emit_instruction_ASL_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ASL_scratch_interp,
           asm_instruction_ASL_scratch_interp_END);
}

void
asm_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CMP_imm_interp,
           asm_instruction_CMP_imm_interp_END);
}

void
asm_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CMP_scratch_interp,
           asm_instruction_CMP_scratch_interp_END);
}

void
asm_emit_instruction_CPX_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CPX_imm_interp,
           asm_instruction_CPX_imm_interp_END);
}

void
asm_emit_instruction_CPX_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CPX_scratch_interp,
           asm_instruction_CPX_scratch_interp_END);
}

void
asm_emit_instruction_CPY_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CPY_imm_interp,
           asm_instruction_CPY_imm_interp_END);

}

void
asm_emit_instruction_CPY_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_CPY_scratch_interp,
           asm_instruction_CPY_scratch_interp_END);
}

void
asm_emit_instruction_DEC_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_DEC_scratch_interp,
           asm_instruction_DEC_scratch_interp_END);
}

void
asm_emit_instruction_EOR_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_EOR_imm_interp,
           asm_instruction_EOR_imm_interp_END);
}

void
asm_emit_instruction_EOR_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_EOR_scratch_interp,
           asm_instruction_EOR_scratch_interp_END);
}

void
asm_emit_instruction_INC_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_INC_scratch_interp,
           asm_instruction_INC_scratch_interp_END);
}

void
asm_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_JMP_scratch_interp,
           asm_instruction_JMP_scratch_interp_END);
}

void
asm_emit_instruction_JSR_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_inturbo_pc_plus_2_to_scratch,
           asm_inturbo_pc_plus_2_to_scratch_END);
  asm_emit_push_word_from_scratch(p_buf);
  asm_copy(p_buf, asm_inturbo_load_pc_from_pc, asm_inturbo_load_pc_from_pc_END);
}

void
asm_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDA_imm_interp,
           asm_instruction_LDA_imm_interp_END);
}

void
asm_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDA_scratch_interp,
           asm_instruction_LDA_scratch_interp_END);
}

void
asm_emit_instruction_LDX_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDX_imm_interp,
           asm_instruction_LDX_imm_interp_END);
}

void
asm_emit_instruction_LDX_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDX_scratch_interp,
           asm_instruction_LDX_scratch_interp_END);
}

void
asm_emit_instruction_LDY_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDY_imm_interp,
           asm_instruction_LDY_imm_interp_END);
}

void
asm_emit_instruction_LDY_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LDY_scratch_interp,
           asm_instruction_LDY_scratch_interp_END);
}

void
asm_emit_instruction_LSR_acc_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LSR_acc_interp,
           asm_instruction_LSR_acc_interp_END);
}

void
asm_emit_instruction_LSR_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_LSR_scratch_interp,
           asm_instruction_LSR_scratch_interp_END);
}

void
asm_emit_instruction_ORA_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ORA_imm_interp,
           asm_instruction_ORA_imm_interp_END);
}

void
asm_emit_instruction_ORA_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ORA_scratch_interp,
           asm_instruction_ORA_scratch_interp_END);
}

void
asm_emit_instruction_ROL_acc_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ROL_acc_interp,
           asm_instruction_ROL_acc_interp_END);
}

void
asm_emit_instruction_ROL_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ROL_scratch_interp,
           asm_instruction_ROL_scratch_interp_END);
}

void
asm_emit_instruction_ROR_acc_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ROR_acc_interp,
           asm_instruction_ROR_acc_interp_END);
}

void
asm_emit_instruction_ROR_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_ROR_scratch_interp,
           asm_instruction_ROR_scratch_interp_END);
}

void
asm_emit_instruction_RTI_interp(struct util_buffer* p_buf) {
  asm_emit_instruction_PLP(p_buf);
  asm_emit_pull_word_to_scratch(p_buf);
  asm_emit_instruction_JMP_scratch_interp(p_buf);
}

void
asm_emit_instruction_RTS_interp(struct util_buffer* p_buf) {
  asm_emit_pull_word_to_scratch(p_buf);
  asm_copy(p_buf,
           asm_inturbo_JMP_scratch_plus_1_interp,
           asm_inturbo_JMP_scratch_plus_1_interp_END);
}

void
asm_emit_instruction_SAX_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_SAX_scratch_interp,
           asm_instruction_SAX_scratch_interp_END);
}

void
asm_emit_instruction_SBC_imm_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_SBC_imm_interp,
           asm_instruction_SBC_imm_interp_END);
}

void
asm_emit_instruction_SBC_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_SBC_scratch_interp,
           asm_instruction_SBC_scratch_interp_END);
}

void
asm_emit_instruction_SLO_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_SLO_scratch_interp,
           asm_instruction_SLO_scratch_interp_END);
}

void
asm_emit_instruction_STA_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_STA_scratch_interp,
           asm_instruction_STA_scratch_interp_END);
}

void
asm_emit_instruction_STX_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_STX_scratch_interp,
           asm_instruction_STX_scratch_interp_END);
}

void
asm_emit_instruction_STY_scratch_interp(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_instruction_STY_scratch_interp,
           asm_instruction_STY_scratch_interp_END);
}
