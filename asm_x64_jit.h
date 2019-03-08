#ifndef BEEBJIT_ASM_X64_JIT_H
#define BEEBJIT_ASM_X64_JIT_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_jit_call_compile_trampoline(struct util_buffer* p_buf);

void asm_x64_emit_jit_FLAGA(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGX(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGY(struct util_buffer* p_buf);
void asm_x64_emit_jit_STOA_IMM(struct util_buffer* p_buf,
                               uint16_t addr,
                               uint8_t value);

void asm_x64_emit_jit_BNE(struct util_buffer* p_buf,
                          int32_t value1,
                          int32_t value2);
void asm_x64_emit_jit_JMP(struct util_buffer* p_buf,
                          int32_t value1,
                          int32_t value2);
void asm_x64_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDA_ABX(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_jit_compile_trampoline();
void asm_x64_jit_do_interrupt();

void asm_x64_jit_call_compile_trampoline();
void asm_x64_jit_call_compile_trampoline_END();

void asm_x64_jit_FLAGA();
void asm_x64_jit_FLAGA_END();
void asm_x64_jit_FLAGX();
void asm_x64_jit_FLAGX_END();
void asm_x64_jit_FLAGY();
void asm_x64_jit_FLAGY_END();
void asm_x64_jit_STOA_IMM();
void asm_x64_jit_STOA_IMM_END();

void asm_x64_jit_BNE();
void asm_x64_jit_BNE_END();
void asm_x64_jit_JMP();
void asm_x64_jit_JMP_END();
void asm_x64_jit_LDA_IMM();
void asm_x64_jit_LDA_IMM_END();
void asm_x64_jit_LDA_ABX();
void asm_x64_jit_LDA_ABX_END();
void asm_x64_jit_LDX_IMM();
void asm_x64_jit_LDX_IMM_END();
void asm_x64_jit_LDY_IMM();
void asm_x64_jit_LDY_IMM_END();
void asm_x64_jit_STA_ABX();
void asm_x64_jit_STA_ABX_END();

#endif /* BEEBJIT_ASM_X64_JIT_H */
