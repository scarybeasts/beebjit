#include "../asm_inturbo.h"

#include "../../util.h"
#include "../asm_common.h"
#include "asm_helper_arm64.h"

#include <assert.h>
/* For ssize_t. */
#include <sys/types.h>

static void
asm_patch_arm64_imm9(struct util_buffer* p_buf, int32_t val) {
  uint8_t* p_raw;
  ssize_t pos;
  uint32_t insn;
  uint32_t* p_insn;
  assert((val <= 255) && (val >= -256));

  p_raw = util_buffer_get_ptr(p_buf);
  pos = util_buffer_get_pos(p_buf);
  assert(pos >= 4);
  pos -= 4;
  p_raw += pos;
  p_insn = (uint32_t*) p_raw;
  insn = *p_insn;
  insn &= ~(0x1FF << 12);
  insn |= ((val & 0x1FF) << 12);
  *p_insn = insn;
}

static void
asm_patch_arm64_imm19_pc_rel(struct util_buffer* p_buf, uint8_t* p_target) {
  uint8_t* p_raw;
  uint8_t* p_src;
  ssize_t pos;
  uint32_t insn;
  uint32_t* p_insn;
  intptr_t delta;

  p_raw = util_buffer_get_ptr(p_buf);
  pos = util_buffer_get_pos(p_buf);
  assert(pos >= 4);
  pos -= 4;
  p_raw += pos;

  p_src = util_buffer_get_base_address(p_buf);
  p_src += pos;
  delta = (p_target - p_src);
  delta /= 4;
  /* Not the correct range for imm19 but too lazy to fix it for now. */
  assert((delta <= 16383) && (delta >= -16384));

  p_insn = (uint32_t*) p_raw;
  insn = *p_insn;
  insn &= ~(0x7FFFF << 5);
  insn |= ((delta & 0x7FFFF) << 5);
  *p_insn = insn;
}

static void
asm_emit_instruction_AND_scratch_interp_common(struct util_buffer* p_buf) {
  void asm_instruction_AND_scratch_interp_common(void);
  void asm_instruction_AND_scratch_interp_common_END(void);
  asm_copy(p_buf,
           asm_instruction_AND_scratch_interp_common,
           asm_instruction_AND_scratch_interp_common_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

static void
asm_emit_instruction_EOR_scratch_interp_common(struct util_buffer* p_buf) {
  void asm_instruction_EOR_scratch_interp_common(void);
  void asm_instruction_EOR_scratch_interp_common_END(void);
  asm_copy(p_buf,
           asm_instruction_EOR_scratch_interp_common,
           asm_instruction_EOR_scratch_interp_common_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

static void
asm_emit_instruction_ORA_scratch_interp_common(struct util_buffer* p_buf) {
  void asm_instruction_ORA_scratch_interp_common(void);
  void asm_instruction_ORA_scratch_interp_common_END(void);
  asm_copy(p_buf,
           asm_instruction_ORA_scratch_interp_common,
           asm_instruction_ORA_scratch_interp_common_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

static void
asm_emit_inturbo_mode_imm(struct util_buffer* p_buf) {
  void asm_inturbo_mode_imm(void);
  void asm_inturbo_mode_imm_END(void);
  asm_copy(p_buf, asm_inturbo_mode_imm, asm_inturbo_mode_imm_END);
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
  void asm_inturbo_jump_call_interp(void);
  void asm_inturbo_jump_call_interp_END(void);
  asm_copy(p_buf,
           asm_inturbo_jump_call_interp,
           asm_inturbo_jump_call_interp_END);
}

void
asm_emit_inturbo_check_special_address(struct util_buffer* p_buf,
                                       uint16_t special_addr_above) {
  uint8_t* p_dest;
  void asm_inturbo_check_special_address_movz(void);
  void asm_inturbo_check_special_address_movz_END(void);
  void asm_inturbo_check_special_address_sub_and_tbz(void);
  void asm_inturbo_check_special_address_sub_and_tbz_END(void);
  asm_copy(p_buf,
           asm_inturbo_check_special_address_movz,
           asm_inturbo_check_special_address_movz_END);
  asm_patch_arm64_imm16(p_buf, (special_addr_above + 1));
  asm_copy(p_buf,
           asm_inturbo_check_special_address_sub_and_tbz,
           asm_inturbo_check_special_address_sub_and_tbz_END);
  p_dest = util_buffer_get_base_address(p_buf);
  p_dest += util_buffer_get_length(p_buf);
  p_dest -= 4;
  asm_patch_arm64_imm14_pc_rel(p_buf, p_dest);
}

void
asm_emit_inturbo_check_countdown(struct util_buffer* p_buf, uint8_t opcycles) {
  uint8_t* p_dest;
  void asm_inturbo_check_countdown_sub(void);
  void asm_inturbo_check_countdown_sub_END(void);
  void asm_inturbo_check_countdown_tbnz(void);
  void asm_inturbo_check_countdown_tbnz_END(void);
  asm_copy(p_buf,
           asm_inturbo_check_countdown_sub,
           asm_inturbo_check_countdown_sub_END);
  asm_patch_arm64_imm12(p_buf, opcycles);
  asm_copy(p_buf,
           asm_inturbo_check_countdown_tbnz,
           asm_inturbo_check_countdown_tbnz_END);
  p_dest = util_buffer_get_base_address(p_buf);
  p_dest += util_buffer_get_length(p_buf);
  p_dest -= 4;
  asm_patch_arm64_imm14_pc_rel(p_buf, p_dest);
}

void
asm_emit_inturbo_commit_branch(struct util_buffer* p_buf) {
  void asm_inturbo_commit_branch(void);
  void asm_inturbo_commit_branch_END(void);
  asm_copy(p_buf, asm_inturbo_commit_branch, asm_inturbo_commit_branch_END);
}

void
asm_emit_inturbo_check_decimal(struct util_buffer* p_buf) {
  uint8_t* p_dest;
  void asm_inturbo_check_decimal_tbnz(void);
  void asm_inturbo_check_decimal_tbnz_END(void);
  asm_copy(p_buf,
           asm_inturbo_check_decimal_tbnz,
           asm_inturbo_check_decimal_tbnz_END);
  p_dest = util_buffer_get_base_address(p_buf);
  p_dest += util_buffer_get_length(p_buf);
  p_dest -= 4;
  asm_patch_arm64_imm14_pc_rel(p_buf, p_dest);
}

void
asm_emit_inturbo_check_interrupt(struct util_buffer* p_buf) {
  uint8_t* p_dest;
  void asm_inturbo_check_interrupt_cbnz(void);
  void asm_inturbo_check_interrupt_cbnz_END(void);
  asm_copy(p_buf,
           asm_inturbo_check_interrupt_cbnz,
           asm_inturbo_check_interrupt_cbnz_END);
  p_dest = util_buffer_get_base_address(p_buf);
  p_dest += util_buffer_get_length(p_buf);
  p_dest -= 4;
  asm_patch_arm64_imm19_pc_rel(p_buf, p_dest);
}

void
asm_emit_inturbo_advance_pc_and_next(struct util_buffer* p_buf,
                                     uint8_t advance) {
  void asm_inturbo_load_and_advance_pc(void);
  void asm_inturbo_load_and_advance_pc_END(void);
  void asm_inturbo_jump_next_opcode(void);
  void asm_inturbo_jump_next_opcode_END(void);
  asm_copy(p_buf,
           asm_inturbo_load_and_advance_pc,
           asm_inturbo_load_and_advance_pc_END);
  asm_patch_arm64_imm9(p_buf, advance);
  asm_copy(p_buf,
           asm_inturbo_jump_next_opcode,
           asm_inturbo_jump_next_opcode_END);
}

void
asm_emit_inturbo_advance_pc_and_ret(struct util_buffer* p_buf,
                                    uint8_t advance) {
  (void) p_buf;
  (void) advance;
}

void
asm_emit_inturbo_enter_debug(struct util_buffer* p_buf) {
  void asm_inturbo_enter_debug(void);
  void asm_inturbo_enter_debug_END(void);
  asm_copy(p_buf, asm_inturbo_enter_debug, asm_inturbo_enter_debug_END);
}

void
asm_emit_inturbo_call_interp(struct util_buffer* p_buf) {
  void asm_inturbo_jump_call_interp(void);
  void asm_inturbo_jump_call_interp_END(void);
  asm_copy(p_buf,
           asm_inturbo_jump_call_interp,
           asm_inturbo_jump_call_interp_END);
}

void
asm_emit_inturbo_do_write_invalidation(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_inturbo_mode_rel(struct util_buffer* p_buf) {
  void asm_inturbo_mode_rel(void);
  void asm_inturbo_mode_rel_END(void);
  asm_copy(p_buf, asm_inturbo_mode_rel, asm_inturbo_mode_rel_END);
}

void
asm_emit_inturbo_mode_zpg(struct util_buffer* p_buf) {
  void asm_inturbo_mode_zpg(void);
  void asm_inturbo_mode_zpg_END(void);
  asm_copy(p_buf, asm_inturbo_mode_zpg, asm_inturbo_mode_zpg_END);
}

void
asm_emit_inturbo_mode_abs(struct util_buffer* p_buf) {
  void asm_inturbo_mode_abs(void);
  void asm_inturbo_mode_abs_END(void);
  asm_copy(p_buf, asm_inturbo_mode_abs, asm_inturbo_mode_abs_END);
}

void
asm_emit_inturbo_mode_abx(struct util_buffer* p_buf) {
  void asm_inturbo_mode_abx(void);
  void asm_inturbo_mode_abx_END(void);
  asm_copy(p_buf, asm_inturbo_mode_abx, asm_inturbo_mode_abx_END);
}

void
asm_emit_inturbo_mode_abx_check_page_crossing(struct util_buffer* p_buf) {
  void asm_inturbo_check_page_crossing(void);
  void asm_inturbo_check_page_crossing_END(void);
  asm_copy(p_buf,
           asm_inturbo_check_page_crossing,
           asm_inturbo_check_page_crossing_END);
}

void
asm_emit_inturbo_mode_aby(struct util_buffer* p_buf) {
  void asm_inturbo_mode_aby(void);
  void asm_inturbo_mode_aby_END(void);
  asm_copy(p_buf, asm_inturbo_mode_aby, asm_inturbo_mode_aby_END);
}

void
asm_emit_inturbo_mode_aby_check_page_crossing(struct util_buffer* p_buf) {
  asm_emit_inturbo_mode_abx_check_page_crossing(p_buf);
}

void
asm_emit_inturbo_mode_zpx(struct util_buffer* p_buf) {
  void asm_inturbo_mode_zpx(void);
  void asm_inturbo_mode_zpx_END(void);
  asm_copy(p_buf, asm_inturbo_mode_zpx, asm_inturbo_mode_zpx_END);
}

void
asm_emit_inturbo_mode_zpy(struct util_buffer* p_buf) {
  void asm_inturbo_mode_zpy(void);
  void asm_inturbo_mode_zpy_END(void);
  asm_copy(p_buf, asm_inturbo_mode_zpy, asm_inturbo_mode_zpy_END);
}

void
asm_emit_inturbo_mode_idx(struct util_buffer* p_buf) {
  void asm_inturbo_mode_idx(void);
  void asm_inturbo_mode_idx_END(void);
  asm_copy(p_buf, asm_inturbo_mode_idx, asm_inturbo_mode_idx_END);
}

void
asm_emit_inturbo_mode_idy(struct util_buffer* p_buf) {
  void asm_inturbo_mode_idy(void);
  void asm_inturbo_mode_idy_END(void);
  asm_copy(p_buf, asm_inturbo_mode_idy, asm_inturbo_mode_idy_END);
}

void
asm_emit_inturbo_mode_idy_check_page_crossing(struct util_buffer* p_buf) {
  asm_emit_inturbo_mode_abx_check_page_crossing(p_buf);
}

void
asm_emit_inturbo_mode_ind(struct util_buffer* p_buf) {
  void asm_inturbo_mode_ind(void);
  void asm_inturbo_mode_ind_END(void);
  asm_copy(p_buf, asm_inturbo_mode_ind, asm_inturbo_mode_ind_END);
}

void
asm_emit_inturbo_fetch_from_scratch(struct util_buffer* p_buf) {
  void asm_inturbo_fetch_from_scratch(void);
  void asm_inturbo_fetch_from_scratch_END(void);
  asm_copy(p_buf,
           asm_inturbo_fetch_from_scratch,
           asm_inturbo_fetch_from_scratch_END);
}

void
asm_emit_instruction_ADC_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_ADC_imm_interp(void);
  void asm_instruction_ADC_imm_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_ADC_imm_interp,
           asm_instruction_ADC_imm_interp_END);
}

void
asm_emit_instruction_ADC_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_ADC_imm_interp(void);
  void asm_instruction_ADC_imm_interp_END(void);
  asm_emit_inturbo_fetch_from_scratch(p_buf);
  asm_copy(p_buf,
           asm_instruction_ADC_imm_interp,
           asm_instruction_ADC_imm_interp_END);
}

void
asm_emit_instruction_ALR_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_ALR_scratch_interp(void);
  void asm_instruction_ALR_scratch_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_ALR_scratch_interp,
           asm_instruction_ALR_scratch_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_AND_imm_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_mode_imm(p_buf);
  asm_emit_instruction_AND_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_AND_scratch_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_fetch_from_scratch(p_buf);
  asm_emit_instruction_AND_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_ASL_acc_interp(struct util_buffer* p_buf) {
  void asm_instruction_ASL_acc_interp(void);
  void asm_instruction_ASL_acc_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_ASL_acc_interp,
           asm_instruction_ASL_acc_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_ASL_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_ASL_scratch_interp(void);
  void asm_instruction_ASL_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_ASL_scratch_interp,
           asm_instruction_ASL_scratch_interp_END);
}

void
asm_emit_instruction_BCC_interp(struct util_buffer* p_buf) {
  void asm_instruction_BCC_interp(void);
  void asm_instruction_BCC_interp_END(void);
  asm_copy(p_buf, asm_instruction_BCC_interp, asm_instruction_BCC_interp_END);
}

void
asm_emit_instruction_BCC_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BCC_interp_accurate(void);
  void asm_instruction_BCC_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BCC_interp_accurate,
           asm_instruction_BCC_interp_accurate_END);
}

void
asm_emit_instruction_BCS_interp(struct util_buffer* p_buf) {
  void asm_instruction_BCS_interp(void);
  void asm_instruction_BCS_interp_END(void);
  asm_copy(p_buf, asm_instruction_BCS_interp, asm_instruction_BCS_interp_END);
}

void
asm_emit_instruction_BCS_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BCS_interp_accurate(void);
  void asm_instruction_BCS_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BCS_interp_accurate,
           asm_instruction_BCS_interp_accurate_END);
}

void
asm_emit_instruction_BEQ_interp(struct util_buffer* p_buf) {
  void asm_instruction_BEQ_interp(void);
  void asm_instruction_BEQ_interp_END(void);
  asm_copy(p_buf, asm_instruction_BEQ_interp, asm_instruction_BEQ_interp_END);
}

void
asm_emit_instruction_BEQ_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BEQ_interp_accurate(void);
  void asm_instruction_BEQ_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BEQ_interp_accurate,
           asm_instruction_BEQ_interp_accurate_END);
}

void
asm_emit_instruction_BIT_interp(struct util_buffer* p_buf) {
  void asm_instruction_BIT_interp(void);
  void asm_instruction_BIT_interp_END(void);
  asm_copy(p_buf, asm_instruction_BIT_interp, asm_instruction_BIT_interp_END);
  asm_emit_instruction_BIT_common(p_buf);
}

void
asm_emit_instruction_BMI_interp(struct util_buffer* p_buf) {
  void asm_instruction_BMI_interp(void);
  void asm_instruction_BMI_interp_END(void);
  asm_copy(p_buf, asm_instruction_BMI_interp, asm_instruction_BMI_interp_END);
}

void
asm_emit_instruction_BMI_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BMI_interp_accurate(void);
  void asm_instruction_BMI_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BMI_interp_accurate,
           asm_instruction_BMI_interp_accurate_END);
}

void
asm_emit_instruction_BNE_interp(struct util_buffer* p_buf) {
  void asm_instruction_BNE_interp(void);
  void asm_instruction_BNE_interp_END(void);
  asm_copy(p_buf, asm_instruction_BNE_interp, asm_instruction_BNE_interp_END);
}

void
asm_emit_instruction_BNE_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BNE_interp_accurate(void);
  void asm_instruction_BNE_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BNE_interp_accurate,
           asm_instruction_BNE_interp_accurate_END);
}

void
asm_emit_instruction_BPL_interp(struct util_buffer* p_buf) {
  void asm_instruction_BPL_interp(void);
  void asm_instruction_BPL_interp_END(void);
  asm_copy(p_buf, asm_instruction_BPL_interp, asm_instruction_BPL_interp_END);
}

void
asm_emit_instruction_BPL_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BPL_interp_accurate(void);
  void asm_instruction_BPL_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BPL_interp_accurate,
           asm_instruction_BPL_interp_accurate_END);
}

void
asm_emit_instruction_BRK_interp(struct util_buffer* p_buf) {
  void asm_inturbo_push_pc(void);
  void asm_inturbo_push_pc_END(void);
  void asm_inturbo_interrupt_vector(void);
  void asm_inturbo_interrupt_vector_END(void);
  asm_copy(p_buf, asm_inturbo_push_pc, asm_inturbo_push_pc_END);
  asm_emit_instruction_PHP(p_buf);
  asm_emit_instruction_SEI(p_buf);
  asm_copy(p_buf,
           asm_inturbo_interrupt_vector,
           asm_inturbo_interrupt_vector_END);
}

void
asm_emit_instruction_BVC_interp(struct util_buffer* p_buf) {
  void asm_instruction_BVC_interp(void);
  void asm_instruction_BVC_interp_END(void);
  asm_copy(p_buf, asm_instruction_BVC_interp, asm_instruction_BVC_interp_END);
}

void
asm_emit_instruction_BVC_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BVC_interp_accurate(void);
  void asm_instruction_BVC_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BVC_interp_accurate,
           asm_instruction_BVC_interp_accurate_END);
}

void
asm_emit_instruction_BVS_interp(struct util_buffer* p_buf) {
  void asm_instruction_BVS_interp(void);
  void asm_instruction_BVS_interp_END(void);
  asm_copy(p_buf, asm_instruction_BVS_interp, asm_instruction_BVS_interp_END);
}

void
asm_emit_instruction_BVS_interp_accurate(struct util_buffer* p_buf) {
  void asm_instruction_BVS_interp_accurate(void);
  void asm_instruction_BVS_interp_accurate_END(void);
  asm_copy(p_buf,
           asm_instruction_BVS_interp_accurate,
           asm_instruction_BVS_interp_accurate_END);
}

void
asm_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_CMP_imm_interp(void);
  void asm_instruction_CMP_imm_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_CMP_imm_interp,
           asm_instruction_CMP_imm_interp_END);
}

void
asm_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_CMP_scratch_interp(void);
  void asm_instruction_CMP_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_CMP_scratch_interp,
           asm_instruction_CMP_scratch_interp_END);
}

void
asm_emit_instruction_CPX_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_CPX_imm_interp(void);
  void asm_instruction_CPX_imm_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_CPX_imm_interp,
           asm_instruction_CPX_imm_interp_END);
}

void
asm_emit_instruction_CPX_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_CPX_scratch_interp(void);
  void asm_instruction_CPX_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_CPX_scratch_interp,
           asm_instruction_CPX_scratch_interp_END);
}

void
asm_emit_instruction_CPY_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_CPY_imm_interp(void);
  void asm_instruction_CPY_imm_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_CPY_imm_interp,
           asm_instruction_CPY_imm_interp_END);
}

void
asm_emit_instruction_CPY_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_CPY_scratch_interp(void);
  void asm_instruction_CPY_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_CPY_scratch_interp,
           asm_instruction_CPY_scratch_interp_END);
}

void
asm_emit_instruction_DEC_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_DEC_scratch_interp(void);
  void asm_instruction_DEC_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_DEC_scratch_interp,
           asm_instruction_DEC_scratch_interp_END);
}

void
asm_emit_instruction_EOR_imm_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_mode_imm(p_buf);
  asm_emit_instruction_EOR_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_EOR_scratch_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_fetch_from_scratch(p_buf);
  asm_emit_instruction_EOR_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_INC_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_INC_scratch_interp(void);
  void asm_instruction_INC_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_INC_scratch_interp,
           asm_instruction_INC_scratch_interp_END);
}

void
asm_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_JMP_scratch_interp(void);
  void asm_instruction_JMP_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_JMP_scratch_interp,
           asm_instruction_JMP_scratch_interp_END);
}

void
asm_emit_instruction_JSR_scratch_interp(struct util_buffer* p_buf) {
  void asm_inturbo_push_pc(void);
  void asm_inturbo_push_pc_END(void);
  asm_copy(p_buf, asm_inturbo_push_pc, asm_inturbo_push_pc_END);
  asm_emit_inturbo_mode_abs(p_buf);
  asm_emit_instruction_JMP_scratch_interp(p_buf);
}

void
asm_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDA_imm_interp(void);
  void asm_instruction_LDA_imm_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDA_imm_interp,
           asm_instruction_LDA_imm_interp_END);
}

void
asm_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDA_scratch_interp(void);
  void asm_instruction_LDA_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDA_scratch_interp,
           asm_instruction_LDA_scratch_interp_END);
}

void
asm_emit_instruction_LDX_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDX_imm_interp(void);
  void asm_instruction_LDX_imm_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDX_imm_interp,
           asm_instruction_LDX_imm_interp_END);
}

void
asm_emit_instruction_LDX_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDX_scratch_interp(void);
  void asm_instruction_LDX_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDX_scratch_interp,
           asm_instruction_LDX_scratch_interp_END);
}

void
asm_emit_instruction_LDY_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDY_imm_interp(void);
  void asm_instruction_LDY_imm_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDY_imm_interp,
           asm_instruction_LDY_imm_interp_END);
}

void
asm_emit_instruction_LDY_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_LDY_scratch_interp(void);
  void asm_instruction_LDY_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LDY_scratch_interp,
           asm_instruction_LDY_scratch_interp_END);
}

void
asm_emit_instruction_LSR_acc_interp(struct util_buffer* p_buf) {
  void asm_instruction_LSR_acc_interp(void);
  void asm_instruction_LSR_acc_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LSR_acc_interp,
           asm_instruction_LSR_acc_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_LSR_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_LSR_scratch_interp(void);
  void asm_instruction_LSR_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_LSR_scratch_interp,
           asm_instruction_LSR_scratch_interp_END);
}

void
asm_emit_instruction_ORA_imm_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_mode_imm(p_buf);
  asm_emit_instruction_ORA_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_ORA_scratch_interp(struct util_buffer* p_buf) {
  asm_emit_inturbo_fetch_from_scratch(p_buf);
  asm_emit_instruction_ORA_scratch_interp_common(p_buf);
}

void
asm_emit_instruction_ROL_acc_interp(struct util_buffer* p_buf) {
  void asm_instruction_ROL_acc_interp(void);
  void asm_instruction_ROL_acc_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_ROL_acc_interp,
           asm_instruction_ROL_acc_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_ROL_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_ROL_scratch_interp(void);
  void asm_instruction_ROL_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_ROL_scratch_interp,
           asm_instruction_ROL_scratch_interp_END);
}

void
asm_emit_instruction_ROR_acc_interp(struct util_buffer* p_buf) {
  void asm_instruction_ROR_acc_interp(void);
  void asm_instruction_ROR_acc_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_ROR_acc_interp,
           asm_instruction_ROR_acc_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_ROR_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_ROR_scratch_interp(void);
  void asm_instruction_ROR_scratch_interp_END(void);
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
  void asm_inturbo_add_1_to_scratch(void);
  void asm_inturbo_add_1_to_scratch_END(void);
  asm_emit_pull_word_to_scratch(p_buf);
  asm_copy(p_buf,
           asm_inturbo_add_1_to_scratch,
           asm_inturbo_add_1_to_scratch_END);
  asm_emit_instruction_JMP_scratch_interp(p_buf);
}

void
asm_emit_instruction_SAX_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_SAX_scratch_interp(void);
  void asm_instruction_SAX_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_SAX_scratch_interp,
           asm_instruction_SAX_scratch_interp_END);
}

void
asm_emit_instruction_SBC_imm_interp(struct util_buffer* p_buf) {
  void asm_instruction_SBC_imm_interp(void);
  void asm_instruction_SBC_imm_interp_END(void);
  asm_emit_inturbo_mode_imm(p_buf);
  asm_copy(p_buf,
           asm_instruction_SBC_imm_interp,
           asm_instruction_SBC_imm_interp_END);
}

void
asm_emit_instruction_SBC_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_SBC_imm_interp(void);
  void asm_instruction_SBC_imm_interp_END(void);
  asm_emit_inturbo_fetch_from_scratch(p_buf);
  asm_copy(p_buf,
           asm_instruction_SBC_imm_interp,
           asm_instruction_SBC_imm_interp_END);
}

void
asm_emit_instruction_SLO_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_SLO_scratch_interp(void);
  void asm_instruction_SLO_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_SLO_scratch_interp,
           asm_instruction_SLO_scratch_interp_END);
  asm_emit_instruction_A_NZ_flags(p_buf);
}

void
asm_emit_instruction_STA_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_STA_scratch_interp(void);
  void asm_instruction_STA_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_STA_scratch_interp,
           asm_instruction_STA_scratch_interp_END);
}

void
asm_emit_instruction_STX_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_STX_scratch_interp(void);
  void asm_instruction_STX_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_STX_scratch_interp,
           asm_instruction_STX_scratch_interp_END);
}

void
asm_emit_instruction_STY_scratch_interp(struct util_buffer* p_buf) {
  void asm_instruction_STY_scratch_interp(void);
  void asm_instruction_STY_scratch_interp_END(void);
  asm_copy(p_buf,
           asm_instruction_STY_scratch_interp,
           asm_instruction_STY_scratch_interp_END);
}
