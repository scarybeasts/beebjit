#ifndef BEEBJIT_ASM_X64_COMMON_H
#define BEEBJIT_ASM_X64_COMMON_H

#include <stddef.h>
#include <stdint.h>

struct util_buffer;

void asm_x64_copy(struct util_buffer* p_buf, void* p_start, void* p_end);
void asm_x64_patch_byte(struct util_buffer* p_buf,
                        size_t offset,
                        void* p_start,
                        void* p_patch,
                        uint8_t value);
void asm_x64_patch_int(struct util_buffer* p_buf,
                       size_t offset,
                       void* p_start,
                       void* p_patch,
                       int value);
void asm_x64_patch_jump(struct util_buffer* p_buf,
                        size_t offset,
                        void* p_start,
                        void* p_patch,
                        void* p_jump_target);

uint32_t asm_x64_asm_enter(void* p_context,
                           uint32_t jump_addr_x64,
                           int64_t countdown);
void asm_x64_asm_debug();
void asm_x64_save_AXYS_PC_flags();
void asm_x64_restore_AXYS_PC_flags();

void asm_x64_emit_instruction_CRASH(struct util_buffer* p_buf);
void asm_x64_emit_instruction_EXIT(struct util_buffer* p_buf);
void asm_x64_emit_instruction_REAL_NOP(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TRAP(struct util_buffer* p_buf);
void asm_x64_emit_instruction_ILLEGAL(struct util_buffer* p_buf);

void asm_x64_emit_instruction_BIT_common(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CLC(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CLD(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CLI(struct util_buffer* p_buf);
void asm_x64_emit_instruction_CLV(struct util_buffer* p_buf);
void asm_x64_emit_instruction_DEX(struct util_buffer* p_buf);
void asm_x64_emit_instruction_DEY(struct util_buffer* p_buf);
void asm_x64_emit_instruction_INX(struct util_buffer* p_buf);
void asm_x64_emit_instruction_INY(struct util_buffer* p_buf);
void asm_x64_emit_instruction_PHA(struct util_buffer* p_buf);
void asm_x64_emit_instruction_PHP(struct util_buffer* p_buf);
void asm_x64_emit_instruction_PLA(struct util_buffer* p_buf);
void asm_x64_emit_instruction_PLP(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SEC(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SED(struct util_buffer* p_buf);
void asm_x64_emit_instruction_SEI(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TAX(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TAY(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TSX(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TXA(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TXS(struct util_buffer* p_buf);
void asm_x64_emit_instruction_TYA(struct util_buffer* p_buf);

void asm_x64_emit_instruction_A_NZ_flags(struct util_buffer* p_buf);
void asm_x64_emit_instruction_X_NZ_flags(struct util_buffer* p_buf);
void asm_x64_emit_instruction_Y_NZ_flags(struct util_buffer* p_buf);

void asm_x64_emit_push_word_from_scratch(struct util_buffer* p_buf);
void asm_x64_emit_pull_word_to_scratch(struct util_buffer* p_buf);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_instruction_CRASH();
void asm_x64_instruction_CRASH_END();
void asm_x64_instruction_EXIT();
void asm_x64_instruction_EXIT_END();
void asm_x64_instruction_REAL_NOP();
void asm_x64_instruction_REAL_NOP_END();
void asm_x64_instruction_TRAP();
void asm_x64_instruction_TRAP_END();
void asm_x64_instruction_ILLEGAL();
void asm_x64_instruction_ILLEGAL_END();

void asm_x64_instruction_BIT_common();
void asm_x64_instruction_BIT_common_END();
void asm_x64_instruction_CLC();
void asm_x64_instruction_CLC_END();
void asm_x64_instruction_CLD();
void asm_x64_instruction_CLD_END();
void asm_x64_instruction_CLI();
void asm_x64_instruction_CLI_END();
void asm_x64_instruction_CLV();
void asm_x64_instruction_CLV_END();
void asm_x64_instruction_DEX();
void asm_x64_instruction_DEX_END();
void asm_x64_instruction_DEY();
void asm_x64_instruction_DEY_END();
void asm_x64_instruction_INX();
void asm_x64_instruction_INX_END();
void asm_x64_instruction_INY();
void asm_x64_instruction_INY_END();
void asm_x64_instruction_PHA();
void asm_x64_instruction_PHA_END();
void asm_x64_instruction_PLA();
void asm_x64_instruction_PLA_END();
void asm_x64_instruction_SEC();
void asm_x64_instruction_SEC_END();
void asm_x64_instruction_SED();
void asm_x64_instruction_SED_END();
void asm_x64_instruction_SEI();
void asm_x64_instruction_SEI_END();
void asm_x64_instruction_TAX();
void asm_x64_instruction_TAX_END();
void asm_x64_instruction_TAY();
void asm_x64_instruction_TAY_END();
void asm_x64_instruction_TSX();
void asm_x64_instruction_TSX_END();
void asm_x64_instruction_TXA();
void asm_x64_instruction_TXA_END();
void asm_x64_instruction_TXS();
void asm_x64_instruction_TXS_END();
void asm_x64_instruction_TYA();
void asm_x64_instruction_TYA_END();

void asm_x64_instruction_A_NZ_flags();
void asm_x64_instruction_A_NZ_flags_END();
void asm_x64_instruction_X_NZ_flags();
void asm_x64_instruction_X_NZ_flags_END();
void asm_x64_instruction_Y_NZ_flags();
void asm_x64_instruction_Y_NZ_flags_END();

void asm_x64_asm_emit_intel_flags_to_scratch();
void asm_x64_asm_emit_intel_flags_to_scratch_END();
void asm_x64_asm_set_intel_flags_from_scratch();
void asm_x64_asm_set_intel_flags_from_scratch_END();
void asm_x64_set_brk_flag_in_scratch();
void asm_x64_set_brk_flag_in_scratch_END();
void asm_x64_push_from_scratch();
void asm_x64_push_from_scratch_END();
void asm_x64_push_word_from_scratch();
void asm_x64_push_word_from_scratch_END();
void asm_x64_pull_to_scratch();
void asm_x64_pull_to_scratch_END();
void asm_x64_pull_word_to_scratch();
void asm_x64_pull_word_to_scratch_END();

#endif /* BEEBJIT_ASM_X64_COMMON_H */
