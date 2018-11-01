#ifndef BEEBJIT_ASM_X64_H
#define BEEBJIT_ASM_X64_H

#include <stddef.h>
#include <stdint.h>

struct util_buffer;

size_t asm_x64_copy(struct util_buffer* p_buf,
                    void* p_start,
                    void* p_end,
                    size_t min_for_padding);

void asm_x64_instruction_CRASH();
void asm_x64_instruction_CRASH_END();
void asm_x64_instruction_EXIT();
void asm_x64_instruction_EXIT_END();
void asm_x64_instruction_TRAP();
void asm_x64_instruction_TRAP_END();

void asm_x64_instruction_A_NZ_flags();
void asm_x64_instruction_A_NZ_flags_END();
void asm_x64_instruction_X_NZ_flags();
void asm_x64_instruction_X_NZ_flags_END();
void asm_x64_instruction_Y_NZ_flags();
void asm_x64_instruction_Y_NZ_flags_END();

void asm_x64_instruction_LDA_imm_interp();
void asm_x64_instruction_LDA_imm_interp_END();
void asm_x64_instruction_LDA_scratch_interp();
void asm_x64_instruction_LDA_scratch_interp_END();
void asm_x64_instruction_TAX();
void asm_x64_instruction_TAX_END();
void asm_x64_instruction_TAY();
void asm_x64_instruction_TAY_END();

void asm_x64_asm_enter(void* p_context, uint32_t jump_addr_x64);
void asm_x64_asm_debug();

void asm_x64_inturbo_next_opcode();
void asm_x64_inturbo_next_opcode_END();
void asm_x64_inturbo_enter_debug();
void asm_x64_inturbo_enter_debug_END();
void asm_x64_inturbo_mode_zpg();
void asm_x64_inturbo_mode_zpg_END();
void asm_x64_inturbo_mode_abs();
void asm_x64_inturbo_mode_abs_END();

#endif /* BEEBJIT_ASM_X64_H */
