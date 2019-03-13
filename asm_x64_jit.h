#ifndef BEEBJIT_ASM_X64_JIT_H
#define BEEBJIT_ASM_X64_JIT_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_jit_call_compile_trampoline(struct util_buffer* p_buf);
void asm_x64_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr);

void asm_x64_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_FLAGA(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGX(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGY(struct util_buffer* p_buf);
void asm_x64_emit_jit_LOAD_CARRY(struct util_buffer* p_buf);
void asm_x64_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAVE_CARRY(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf);
void asm_x64_emit_jit_STOA_IMM(struct util_buffer* p_buf,
                               uint16_t addr,
                               uint8_t value);

void asm_x64_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_BCC(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BCS(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BMI(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BNE(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BPL(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BVC(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BVS(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_INC_ZPG(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_JMP(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDA_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDA_ABX(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_jit_compile_trampoline();
void asm_x64_jit_do_interrupt();

void asm_x64_jit_call_compile_trampoline();
void asm_x64_jit_call_compile_trampoline_END();
void asm_x64_jit_call_debug();
void asm_x64_jit_call_debug_pc_patch();
void asm_x64_jit_call_debug_call_patch();
void asm_x64_jit_call_debug_END();

void asm_x64_jit_ADD_IMM();
void asm_x64_jit_ADD_IMM_END();
void asm_x64_jit_FLAGA();
void asm_x64_jit_FLAGA_END();
void asm_x64_jit_FLAGX();
void asm_x64_jit_FLAGX_END();
void asm_x64_jit_FLAGY();
void asm_x64_jit_FLAGY_END();
void asm_x64_jit_LOAD_CARRY();
void asm_x64_jit_LOAD_CARRY_END();
void asm_x64_jit_LOAD_OVERFLOW();
void asm_x64_jit_LOAD_OVERFLOW_END();
void asm_x64_jit_SAVE_CARRY();
void asm_x64_jit_SAVE_CARRY_END();
void asm_x64_jit_SAVE_CARRY_INV();
void asm_x64_jit_SAVE_CARRY_INV_END();
void asm_x64_jit_SAVE_OVERFLOW();
void asm_x64_jit_SAVE_OVERFLOW_END();
void asm_x64_jit_STOA_IMM();
void asm_x64_jit_STOA_IMM_END();

void asm_x64_jit_ADC_IMM();
void asm_x64_jit_ADC_IMM_END();
void asm_x64_jit_BCC();
void asm_x64_jit_BCC_END();
void asm_x64_jit_BCC_8bit();
void asm_x64_jit_BCC_8bit_END();
void asm_x64_jit_BCS();
void asm_x64_jit_BCS_END();
void asm_x64_jit_BCS_8bit();
void asm_x64_jit_BCS_8bit_END();
void asm_x64_jit_BEQ();
void asm_x64_jit_BEQ_END();
void asm_x64_jit_BEQ_8bit();
void asm_x64_jit_BEQ_8bit_END();
void asm_x64_jit_BMI();
void asm_x64_jit_BMI_END();
void asm_x64_jit_BMI_8bit();
void asm_x64_jit_BMI_8bit_END();
void asm_x64_jit_BNE();
void asm_x64_jit_BNE_END();
void asm_x64_jit_BNE_8bit();
void asm_x64_jit_BNE_8bit_END();
void asm_x64_jit_BPL();
void asm_x64_jit_BPL_END();
void asm_x64_jit_BPL_8bit();
void asm_x64_jit_BPL_8bit_END();
void asm_x64_jit_BVC();
void asm_x64_jit_BVC_END();
void asm_x64_jit_BVC_8bit();
void asm_x64_jit_BVC_8bit_END();
void asm_x64_jit_BVS();
void asm_x64_jit_BVS_END();
void asm_x64_jit_BVS_8bit();
void asm_x64_jit_BVS_8bit_END();
void asm_x64_jit_CMP_IMM();
void asm_x64_jit_CMP_IMM_END();
void asm_x64_jit_CPX_IMM();
void asm_x64_jit_CPX_IMM_END();
void asm_x64_jit_CPY_IMM();
void asm_x64_jit_CPY_IMM_END();
void asm_x64_jit_INC_ZPG();
void asm_x64_jit_INC_ZPG_END();
void asm_x64_jit_JMP();
void asm_x64_jit_JMP_END();
void asm_x64_jit_JMP_8bit();
void asm_x64_jit_JMP_8bit_END();
void asm_x64_jit_LDA_IMM();
void asm_x64_jit_LDA_IMM_END();
void asm_x64_jit_LDA_ABS();
void asm_x64_jit_LDA_ABS_END();
void asm_x64_jit_LDA_ABX();
void asm_x64_jit_LDA_ABX_END();
void asm_x64_jit_LDX_IMM();
void asm_x64_jit_LDX_IMM_END();
void asm_x64_jit_LDY_IMM();
void asm_x64_jit_LDY_IMM_END();
void asm_x64_jit_STA_ABX();
void asm_x64_jit_STA_ABX_END();

#endif /* BEEBJIT_ASM_X64_JIT_H */
