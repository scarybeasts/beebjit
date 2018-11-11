#ifndef BEEBJIT_ASM_X64_INTURBO_H
#define BEEBJIT_ASM_X64_INTURBO_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_inturbo_advance_pc_1(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_advance_pc_2(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_advance_pc_3(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_next_opcode(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_enter_debug(struct util_buffer* p_buf);

void asm_x64_emit_inturbo_mode_zpg(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_abs(struct util_buffer* p_buf,
                                   uint16_t special_mode_above);
void asm_x64_emit_inturbo_mode_abx(struct util_buffer* p_buf,
                                   uint16_t special_mode_above);
void asm_x64_emit_inturbo_mode_aby(struct util_buffer* p_buf,
                                   uint16_t special_mode_above);
void asm_x64_emit_inturbo_mode_zpx(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_zpy(struct util_buffer* p_buf);
void asm_x64_emit_inturbo_mode_idx(struct util_buffer* p_buf,
                                   uint16_t special_mode_above);
void asm_x64_emit_inturbo_mode_idy(struct util_buffer* p_buf,
                                   uint16_t special_mode_above);

void asm_x64_emit_instruction_ADC_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ADC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_AND_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_AND_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ASL_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ASL_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCC_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BCS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BEQ_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BIT_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BMI_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BNE_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BPL_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BRK_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVC_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_BVS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPX_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPY_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CPY_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_DEC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_INC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_JMP_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_JSR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDX_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDX_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDY_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_LDY_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROR_acc_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ROR_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_RTI_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_RTS_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SBC_imm_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SBC_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_STA_scratch_interp(struct util_buffer* p_buf);
void asm_x64_emit_instruction_STX_scratch_interp(struct util_buffer* p_buf);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_inturbo_JMP_scratch_plus_1_interp();
void asm_x64_inturbo_JMP_scratch_plus_1_interp_END();
void asm_x64_inturbo_load_pc_from_pc();
void asm_x64_inturbo_load_pc_from_pc_END();
void asm_x64_inturbo_advance_pc_1();
void asm_x64_inturbo_advance_pc_1_END();
void asm_x64_inturbo_advance_pc_2();
void asm_x64_inturbo_advance_pc_2_END();
void asm_x64_inturbo_advance_pc_3();
void asm_x64_inturbo_advance_pc_3_END();
void asm_x64_inturbo_next_opcode();
void asm_x64_inturbo_next_opcode_END();
void asm_x64_inturbo_enter_debug();
void asm_x64_inturbo_enter_debug_END();
void asm_x64_inturbo_pc_plus_2_to_scratch();
void asm_x64_inturbo_pc_plus_2_to_scratch_END();
void asm_x64_inturbo_interrupt_vector();
void asm_x64_inturbo_interrupt_vector_END();
void asm_x64_inturbo_do_special_addr();
void asm_x64_inturbo_check_special_addr();
void asm_x64_inturbo_check_special_addr_END();
void asm_x64_inturbo_check_special_addr_lea_patch();
void asm_x64_inturbo_check_special_addr_jb_patch();

void asm_x64_inturbo_mode_nil();
void asm_x64_inturbo_mode_nil_END();
void asm_x64_inturbo_mode_imm();
void asm_x64_inturbo_mode_imm_END();
void asm_x64_inturbo_mode_zpg();
void asm_x64_inturbo_mode_zpg_END();
void asm_x64_inturbo_mode_abs();
void asm_x64_inturbo_mode_abs_END();
void asm_x64_inturbo_mode_abs_lea_patch();
void asm_x64_inturbo_mode_abs_jb_patch();
void asm_x64_inturbo_mode_abx();
void asm_x64_inturbo_mode_abx_END();
void asm_x64_inturbo_mode_aby();
void asm_x64_inturbo_mode_aby_END();
void asm_x64_inturbo_mode_zpx();
void asm_x64_inturbo_mode_zpx_END();
void asm_x64_inturbo_mode_zpy();
void asm_x64_inturbo_mode_zpy_END();
void asm_x64_inturbo_mode_idx();
void asm_x64_inturbo_mode_idx_END();
void asm_x64_inturbo_mode_idy();
void asm_x64_inturbo_mode_idy_END();

void asm_x64_instruction_ADC_imm_interp();
void asm_x64_instruction_ADC_imm_interp_END();
void asm_x64_instruction_ADC_scratch_interp();
void asm_x64_instruction_ADC_scratch_interp_END();
void asm_x64_instruction_AND_imm_interp();
void asm_x64_instruction_AND_imm_interp_END();
void asm_x64_instruction_AND_scratch_interp();
void asm_x64_instruction_AND_scratch_interp_END();
void asm_x64_instruction_ASL_acc_interp();
void asm_x64_instruction_ASL_acc_interp_END();
void asm_x64_instruction_ASL_scratch_interp();
void asm_x64_instruction_ASL_scratch_interp_END();
void asm_x64_instruction_BCC_interp();
void asm_x64_instruction_BCC_interp_END();
void asm_x64_instruction_BCS_interp();
void asm_x64_instruction_BCS_interp_END();
void asm_x64_instruction_BEQ_interp();
void asm_x64_instruction_BEQ_interp_END();
void asm_x64_instruction_BIT_interp();
void asm_x64_instruction_BIT_interp_END();
void asm_x64_instruction_BMI_interp();
void asm_x64_instruction_BMI_interp_END();
void asm_x64_instruction_BNE_interp();
void asm_x64_instruction_BNE_interp_END();
void asm_x64_instruction_BPL_interp();
void asm_x64_instruction_BPL_interp_END();
void asm_x64_instruction_BVC_interp();
void asm_x64_instruction_BVC_interp_END();
void asm_x64_instruction_BVS_interp();
void asm_x64_instruction_BVS_interp_END();
void asm_x64_instruction_CMP_imm_interp();
void asm_x64_instruction_CMP_imm_interp_END();
void asm_x64_instruction_CMP_scratch_interp();
void asm_x64_instruction_CMP_scratch_interp_END();
void asm_x64_instruction_CPX_imm_interp();
void asm_x64_instruction_CPX_imm_interp_END();
void asm_x64_instruction_CPX_scratch_interp();
void asm_x64_instruction_CPX_scratch_interp_END();
void asm_x64_instruction_CPY_imm_interp();
void asm_x64_instruction_CPY_imm_interp_END();
void asm_x64_instruction_CPY_scratch_interp();
void asm_x64_instruction_CPY_scratch_interp_END();
void asm_x64_instruction_DEC_scratch_interp();
void asm_x64_instruction_DEC_scratch_interp_END();
void asm_x64_instruction_INC_scratch_interp();
void asm_x64_instruction_INC_scratch_interp_END();
void asm_x64_instruction_JMP_scratch_interp();
void asm_x64_instruction_JMP_scratch_interp_END();
void asm_x64_instruction_JSR_scratch_interp();
void asm_x64_instruction_JSR_scratch_interp_END();
void asm_x64_instruction_LDA_imm_interp();
void asm_x64_instruction_LDA_imm_interp_END();
void asm_x64_instruction_LDA_scratch_interp();
void asm_x64_instruction_LDA_scratch_interp_END();
void asm_x64_instruction_LDX_imm_interp();
void asm_x64_instruction_LDX_imm_interp_END();
void asm_x64_instruction_LDX_scratch_interp();
void asm_x64_instruction_LDX_scratch_interp_END();
void asm_x64_instruction_LDY_imm_interp();
void asm_x64_instruction_LDY_imm_interp_END();
void asm_x64_instruction_LDY_scratch_interp();
void asm_x64_instruction_LDY_scratch_interp_END();
void asm_x64_instruction_ROR_acc_interp();
void asm_x64_instruction_ROR_acc_interp_END();
void asm_x64_instruction_ROR_scratch_interp();
void asm_x64_instruction_ROR_scratch_interp_END();
void asm_x64_instruction_SBC_imm_interp();
void asm_x64_instruction_SBC_imm_interp_END();
void asm_x64_instruction_SBC_scratch_interp();
void asm_x64_instruction_SBC_scratch_interp_END();
void asm_x64_instruction_STA_scratch_interp();
void asm_x64_instruction_STA_scratch_interp_END();
void asm_x64_instruction_STX_scratch_interp();
void asm_x64_instruction_STX_scratch_interp_END();

#endif /* BEEBJIT_ASM_X64_INTURBO_H */
