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
void asm_x64_patch_u16(struct util_buffer* p_buf,
                       size_t offset,
                       void* p_start,
                       void* p_patch,
                       uint16_t value);
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
void asm_x64_copy_patch_byte(struct util_buffer* p_buf,
                             void* p_start,
                             void* p_end,
                             uint8_t value);
void asm_x64_copy_patch_u32(struct util_buffer* p_buf,
                            void* p_start,
                            void* p_end,
                            uint32_t value);

ASM_SYMBOL(uint32_t,asm_x64_asm_enter,(void* p_context,
                                       uint32_t jump_addr_x64,
                                       int64_t countdown,
                                       void* p_mem_base));
ASM_SYMBOL(void,asm_x64_asm_debug,());
ASM_SYMBOL(void,asm_x64_save_AXYS_PC_flags,());
ASM_SYMBOL(void,asm_x64_restore_AXYS_PC_flags,());

void asm_x64_emit_instruction_CRASH(struct util_buffer* p_buf);
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

ASM_SYMBOL(void,asm_x64_instruction_CRASH,());
ASM_SYMBOL(void,asm_x64_instruction_CRASH_END,());
ASM_SYMBOL(void,asm_x64_instruction_EXIT,());
ASM_SYMBOL(void,asm_x64_instruction_EXIT_END,());
ASM_SYMBOL(void,asm_x64_instruction_REAL_NOP,());
ASM_SYMBOL(void,asm_x64_instruction_REAL_NOP_END,());
ASM_SYMBOL(void,asm_x64_instruction_TRAP,());
ASM_SYMBOL(void,asm_x64_instruction_TRAP_END,());
ASM_SYMBOL(void,asm_x64_instruction_ILLEGAL,());
ASM_SYMBOL(void,asm_x64_instruction_ILLEGAL_END,());

ASM_SYMBOL(void,asm_x64_instruction_BIT_common,());
ASM_SYMBOL(void,asm_x64_instruction_BIT_common_END,());
ASM_SYMBOL(void,asm_x64_instruction_CLC,());
ASM_SYMBOL(void,asm_x64_instruction_CLC_END,());
ASM_SYMBOL(void,asm_x64_instruction_CLD,());
ASM_SYMBOL(void,asm_x64_instruction_CLD_END,());
ASM_SYMBOL(void,asm_x64_instruction_CLI,());
ASM_SYMBOL(void,asm_x64_instruction_CLI_END,());
ASM_SYMBOL(void,asm_x64_instruction_CLV,());
ASM_SYMBOL(void,asm_x64_instruction_CLV_END,());
ASM_SYMBOL(void,asm_x64_instruction_DEX,());
ASM_SYMBOL(void,asm_x64_instruction_DEX_END,());
ASM_SYMBOL(void,asm_x64_instruction_DEY,());
ASM_SYMBOL(void,asm_x64_instruction_DEY_END,());
ASM_SYMBOL(void,asm_x64_instruction_INX,());
ASM_SYMBOL(void,asm_x64_instruction_INX_END,());
ASM_SYMBOL(void,asm_x64_instruction_INY,());
ASM_SYMBOL(void,asm_x64_instruction_INY_END,());
ASM_SYMBOL(void,asm_x64_instruction_PHA,());
ASM_SYMBOL(void,asm_x64_instruction_PHA_END,());
ASM_SYMBOL(void,asm_x64_instruction_PLA,());
ASM_SYMBOL(void,asm_x64_instruction_PLA_END,());
ASM_SYMBOL(void,asm_x64_instruction_SEC,());
ASM_SYMBOL(void,asm_x64_instruction_SEC_END,());
ASM_SYMBOL(void,asm_x64_instruction_SED,());
ASM_SYMBOL(void,asm_x64_instruction_SED_END,());
ASM_SYMBOL(void,asm_x64_instruction_SEI,());
ASM_SYMBOL(void,asm_x64_instruction_SEI_END,());
ASM_SYMBOL(void,asm_x64_instruction_TAX,());
ASM_SYMBOL(void,asm_x64_instruction_TAX_END,());
ASM_SYMBOL(void,asm_x64_instruction_TAY,());
ASM_SYMBOL(void,asm_x64_instruction_TAY_END,());
ASM_SYMBOL(void,asm_x64_instruction_TSX,());
ASM_SYMBOL(void,asm_x64_instruction_TSX_END,());
ASM_SYMBOL(void,asm_x64_instruction_TXA,());
ASM_SYMBOL(void,asm_x64_instruction_TXA_END,());
ASM_SYMBOL(void,asm_x64_instruction_TXS,());
ASM_SYMBOL(void,asm_x64_instruction_TXS_END,());
ASM_SYMBOL(void,asm_x64_instruction_TYA,());
ASM_SYMBOL(void,asm_x64_instruction_TYA_END,());

ASM_SYMBOL(void,asm_x64_instruction_A_NZ_flags,());
ASM_SYMBOL(void,asm_x64_instruction_A_NZ_flags_END,());
ASM_SYMBOL(void,asm_x64_instruction_X_NZ_flags,());
ASM_SYMBOL(void,asm_x64_instruction_X_NZ_flags_END,());
ASM_SYMBOL(void,asm_x64_instruction_Y_NZ_flags,());
ASM_SYMBOL(void,asm_x64_instruction_Y_NZ_flags_END,());

ASM_SYMBOL(void,asm_x64_asm_emit_intel_flags_to_scratch,());
ASM_SYMBOL(void,asm_x64_asm_emit_intel_flags_to_scratch_END,());
ASM_SYMBOL(void,asm_x64_asm_set_intel_flags_from_scratch,());
ASM_SYMBOL(void,asm_x64_asm_set_intel_flags_from_scratch_END,());
ASM_SYMBOL(void,asm_x64_set_brk_flag_in_scratch,());
ASM_SYMBOL(void,asm_x64_set_brk_flag_in_scratch_END,());
ASM_SYMBOL(void,asm_x64_push_from_scratch,());
ASM_SYMBOL(void,asm_x64_push_from_scratch_END,());
ASM_SYMBOL(void,asm_x64_push_word_from_scratch,());
ASM_SYMBOL(void,asm_x64_push_word_from_scratch_END,());
ASM_SYMBOL(void,asm_x64_pull_to_scratch,());
ASM_SYMBOL(void,asm_x64_pull_to_scratch_END,());
ASM_SYMBOL(void,asm_x64_pull_word_to_scratch,());
ASM_SYMBOL(void,asm_x64_pull_word_to_scratch_END,());

#endif /* BEEBJIT_ASM_X64_COMMON_H */
