#ifndef BEEBJIT_ASM_X64_JIT_H
#define BEEBJIT_ASM_X64_JIT_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_jit_call_compile_trampoline(struct util_buffer* p_buf);

void asm_x64_emit_jit_FLAGA(struct util_buffer* p_buf);
void asm_x64_emit_jit_LODA_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_STOA_IMM(struct util_buffer* p_buf,
                               uint16_t addr,
                               uint8_t value);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_jit_compile_trampoline();
void asm_x64_jit_do_interrupt();

void asm_x64_jit_call_compile_trampoline();
void asm_x64_jit_call_compile_trampoline_END();

void asm_x64_jit_FLAGA();
void asm_x64_jit_FLAGA_END();
void asm_x64_jit_LODA_IMM();
void asm_x64_jit_LODA_IMM_END();
void asm_x64_jit_STOA_IMM();
void asm_x64_jit_STOA_IMM_END();

#endif /* BEEBJIT_ASM_X64_JIT_H */
