#ifndef BEEBJIT_ASM_X64_INTURBO_H
#define BEEBJIT_ASM_X64_INTURBO_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_inturbo_check_special_address(struct util_buffer* p_buf,
                                                uint16_t special_mode_above);
void asm_x64_emit_inturbo_check_countdown(struct util_buffer* p_buf,
                                          uint8_t opcycles);
void asm_x64_emit_inturbo_check_countdown_with_page_crossing(
    struct util_buffer* p_buf, uint8_t opcycles);
void asm_x64_emit_inturbo_check_decimal(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_check_interrupt(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_advance_pc_and_next(struct util_buffer* p_buf,
                                              uint8_t advance);
void asm_x64_emit_inturbo_enter_debug(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_call_interp(struct util_buffer* p_buf);

void asm_x64_emit_inturbo_mode_zpg(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_abs(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_abx(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_abx_check_page_crossing(
    struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_aby(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_aby_check_page_crossing(
    struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_zpx(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_zpy(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_idx(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_idy(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_idy_check_page_crossing(
    struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_ind(struct util_buffer* p_buf);

void asm_x64_emit_instruction_ADC_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ADC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ADC_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_ALR_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_AND_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_AND_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_AND_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_ASL_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ASL_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ASL_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCC_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCC_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCS_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BEQ_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BEQ_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BIT_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BMI_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BMI_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BNE_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BNE_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BPL_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BPL_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BRK_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVC_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVC_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVS_interp_accurate(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CMP_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPX_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPY_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPY_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_DEC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_DEC_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_EOR_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_EOR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_EOR_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_INC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_INC_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_JSR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDA_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDX_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDX_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDY_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDY_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDY_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_LSR_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LSR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LSR_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_ORA_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ORA_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ORA_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROL_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROL_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROL_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROR_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROR_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_RTI_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_RTS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SAX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SBC_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SBC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SBC_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_SLO_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_STA_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_STA_scratch_interp_based(
    struct util_buffer* p_buf);
void asm_x64_emit_instruction_STX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_STY_scratch_interp(struct util_buffer* p_buf);

/* Symbols pointing directly to ASM bytes. */
ASM_SYMBOL(void,asm_x64_inturbo_check_special_address,());
ASM_SYMBOL(void,asm_x64_inturbo_check_special_address_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_special_address_lea_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_special_address_jb_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_lea_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_jb_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_with_page_crossing,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_with_page_crossing_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_with_page_crossing_lea_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_countdown_with_page_crossing_jb_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_check_decimal,());
ASM_SYMBOL(void,asm_x64_inturbo_check_decimal_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_decimal_jb_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_load_opcode,());
ASM_SYMBOL(void,asm_x64_inturbo_load_opcode_END,());
ASM_SYMBOL(void,asm_x64_inturbo_load_opcode_mov_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_advance_pc,());
ASM_SYMBOL(void,asm_x64_inturbo_advance_pc_END,());
ASM_SYMBOL(void,asm_x64_inturbo_advance_pc_lea_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_jump_opcode,());
ASM_SYMBOL(void,asm_x64_inturbo_jump_opcode_END,());

ASM_SYMBOL(void,asm_x64_inturbo_JMP_scratch_plus_1_interp,());
ASM_SYMBOL(void,asm_x64_inturbo_JMP_scratch_plus_1_interp_END,());
ASM_SYMBOL(void,asm_x64_inturbo_load_pc_from_pc,());
ASM_SYMBOL(void,asm_x64_inturbo_load_pc_from_pc_END,());
ASM_SYMBOL(void,asm_x64_inturbo_call_interp,());
ASM_SYMBOL(void,asm_x64_inturbo_call_interp_countdown,());
ASM_SYMBOL(void,asm_x64_inturbo_enter_debug,());
ASM_SYMBOL(void,asm_x64_inturbo_enter_debug_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_interrupt,());
ASM_SYMBOL(void,asm_x64_inturbo_check_interrupt_END,());
ASM_SYMBOL(void,asm_x64_inturbo_check_interrupt_jae_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_jump_call_interp,());
ASM_SYMBOL(void,asm_x64_inturbo_jump_call_interp_END,());
ASM_SYMBOL(void,asm_x64_inturbo_jump_call_interp_jmp_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_pc_plus_2_to_scratch,());
ASM_SYMBOL(void,asm_x64_inturbo_pc_plus_2_to_scratch_END,());
ASM_SYMBOL(void,asm_x64_inturbo_interrupt_vector,());
ASM_SYMBOL(void,asm_x64_inturbo_interrupt_vector_END,());
ASM_SYMBOL(void,asm_x64_inturbo_do_special_addr,());

ASM_SYMBOL(void,asm_x64_inturbo_mode_nil,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_nil_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_imm,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_imm_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpg,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpg_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abs,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abs_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abs_lea_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abs_jb_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abx,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abx_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abx_check_page_crossing,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_abx_check_page_crossing_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_aby,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_aby_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_aby_check_page_crossing,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_aby_check_page_crossing_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpx,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpx_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpy,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_zpy_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idx,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idx_jump_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idx_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idy,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idy_jump_patch,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idy_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idy_check_page_crossing,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_idy_check_page_crossing_END,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_ind,());
ASM_SYMBOL(void,asm_x64_inturbo_mode_ind_END,());

ASM_SYMBOL(void,asm_x64_instruction_Bxx_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_Bxx_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_Bxx_interp_accurate_not_taken_target,());
ASM_SYMBOL(void,asm_x64_instruction_Bxx_interp_accurate_jb_patch,());

ASM_SYMBOL(void,asm_x64_instruction_ADC_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ADC_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ADC_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ADC_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ADC_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_ADC_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_ALR_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ALR_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_AND_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_AND_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_AND_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_AND_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_AND_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_AND_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_acc_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_acc_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_ASL_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_BCC_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BCC_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BCC_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BCC_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BCC_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BCS_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BCS_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BCS_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BCS_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BCS_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BEQ_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BEQ_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BEQ_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BEQ_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BEQ_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BIT_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BIT_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BMI_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BMI_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BMI_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BMI_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BMI_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BNE_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BNE_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BNE_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BNE_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BNE_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BPL_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BPL_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BPL_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BPL_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BPL_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BVC_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BVC_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BVC_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BVC_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BVC_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_BVS_interp,());
ASM_SYMBOL(void,asm_x64_instruction_BVS_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_BVS_interp_accurate,());
ASM_SYMBOL(void,asm_x64_instruction_BVS_interp_accurate_END,());
ASM_SYMBOL(void,asm_x64_instruction_BVS_interp_accurate_jump_patch,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_CMP_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_CPX_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CPX_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_CPX_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CPX_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_CPY_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CPY_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_CPY_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_CPY_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_DEC_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_DEC_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_DEC_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_DEC_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_EOR_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_INC_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_INC_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_INC_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_INC_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_JMP_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_JMP_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_JSR_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_JSR_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_LDA_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_LDX_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_LDY_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_acc_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_acc_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_LSR_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_ORA_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_acc_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_acc_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_ROL_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_acc_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_acc_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_ROR_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_SAX_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_SAX_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_imm_interp,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_imm_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_SBC_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_SLO_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_SLO_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_STA_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_STA_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_STA_scratch_interp_based,());
ASM_SYMBOL(void,asm_x64_instruction_STA_scratch_interp_based_END,());
ASM_SYMBOL(void,asm_x64_instruction_STX_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_STX_scratch_interp_END,());
ASM_SYMBOL(void,asm_x64_instruction_STY_scratch_interp,());
ASM_SYMBOL(void,asm_x64_instruction_STY_scratch_interp_END,());

#endif /* BEEBJIT_ASM_X64_INTURBO_H */
