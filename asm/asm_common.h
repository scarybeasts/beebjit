#ifndef BEEBJIT_ASM_COMMON_H
#define BEEBJIT_ASM_COMMON_H

#include <stddef.h>
#include <stdint.h>

struct util_buffer;

void asm_copy(struct util_buffer* p_buf, void* p_start, void* p_end);
void asm_fill_with_trap(struct util_buffer* p_buf);

void asm_emit_instruction_REAL_NOP(struct util_buffer* p_buf);
void asm_emit_instruction_TRAP(struct util_buffer* p_buf);
void asm_emit_instruction_ILLEGAL(struct util_buffer* p_buf);

void asm_emit_instruction_BIT_value(struct util_buffer* p_buf);
void asm_emit_instruction_CLC(struct util_buffer* p_buf);
void asm_emit_instruction_CLD(struct util_buffer* p_buf);
void asm_emit_instruction_CLI(struct util_buffer* p_buf);
void asm_emit_instruction_CLV(struct util_buffer* p_buf);
void asm_emit_instruction_DEX(struct util_buffer* p_buf);
void asm_emit_instruction_DEY(struct util_buffer* p_buf);
void asm_emit_instruction_INX(struct util_buffer* p_buf);
void asm_emit_instruction_INY(struct util_buffer* p_buf);
void asm_emit_instruction_PHA(struct util_buffer* p_buf);
void asm_emit_instruction_PHP(struct util_buffer* p_buf);
void asm_emit_instruction_PLA(struct util_buffer* p_buf);
void asm_emit_instruction_PLP(struct util_buffer* p_buf);
void asm_emit_instruction_SEC(struct util_buffer* p_buf);
void asm_emit_instruction_SED(struct util_buffer* p_buf);
void asm_emit_instruction_SEI(struct util_buffer* p_buf);
void asm_emit_instruction_TAX(struct util_buffer* p_buf);
void asm_emit_instruction_TAY(struct util_buffer* p_buf);
void asm_emit_instruction_TSX(struct util_buffer* p_buf);
void asm_emit_instruction_TXA(struct util_buffer* p_buf);
void asm_emit_instruction_TXS(struct util_buffer* p_buf);
void asm_emit_instruction_TYA(struct util_buffer* p_buf);

void asm_emit_instruction_A_NZ_flags(struct util_buffer* p_buf);
void asm_emit_instruction_X_NZ_flags(struct util_buffer* p_buf);
void asm_emit_instruction_Y_NZ_flags(struct util_buffer* p_buf);

void asm_emit_push_word_from_scratch(struct util_buffer* p_buf);
void asm_emit_pull_word_to_scratch(struct util_buffer* p_buf);

/* Symbols pointing directly to ASM bytes. */
void asm_debug();
void asm_save_AXYS_PC_flags();
void asm_restore_AXYS_PC_flags();

void asm_instruction_EXIT();
void asm_instruction_EXIT_END();
void asm_instruction_REAL_NOP();
void asm_instruction_REAL_NOP_END();
void asm_instruction_TRAP();
void asm_instruction_TRAP_END();
void asm_instruction_ILLEGAL();
void asm_instruction_ILLEGAL_END();

void asm_instruction_BIT_value();
void asm_instruction_BIT_value_END();
void asm_instruction_CLC();
void asm_instruction_CLC_END();
void asm_instruction_CLD();
void asm_instruction_CLD_END();
void asm_instruction_CLI();
void asm_instruction_CLI_END();
void asm_instruction_CLV();
void asm_instruction_CLV_END();
void asm_instruction_DEX();
void asm_instruction_DEX_END();
void asm_instruction_DEY();
void asm_instruction_DEY_END();
void asm_instruction_INX();
void asm_instruction_INX_END();
void asm_instruction_INY();
void asm_instruction_INY_END();
void asm_instruction_PHA();
void asm_instruction_PHA_END();
void asm_instruction_PLA();
void asm_instruction_PLA_END();
void asm_instruction_SEC();
void asm_instruction_SEC_END();
void asm_instruction_SED();
void asm_instruction_SED_END();
void asm_instruction_SEI();
void asm_instruction_SEI_END();
void asm_instruction_TAX();
void asm_instruction_TAX_END();
void asm_instruction_TAY();
void asm_instruction_TAY_END();
void asm_instruction_TSX();
void asm_instruction_TSX_END();
void asm_instruction_TXA();
void asm_instruction_TXA_END();
void asm_instruction_TXS();
void asm_instruction_TXS_END();
void asm_instruction_TYA();
void asm_instruction_TYA_END();

void asm_instruction_A_NZ_flags();
void asm_instruction_A_NZ_flags_END();
void asm_instruction_X_NZ_flags();
void asm_instruction_X_NZ_flags_END();
void asm_instruction_Y_NZ_flags();
void asm_instruction_Y_NZ_flags_END();

void asm_asm_emit_intel_flags_to_scratch();
void asm_asm_emit_intel_flags_to_scratch_END();
void asm_asm_set_intel_flags_from_scratch();
void asm_asm_set_intel_flags_from_scratch_END();
void asm_set_brk_flag_in_scratch();
void asm_set_brk_flag_in_scratch_END();
void asm_push_from_scratch();
void asm_push_from_scratch_END();
void asm_push_word_from_scratch();
void asm_push_word_from_scratch_END();
void asm_pull_to_scratch();
void asm_pull_to_scratch_END();
void asm_pull_word_to_scratch();
void asm_pull_word_to_scratch_END();

/* TODO: these copy / patch routines shouldn't be here as they are Intel
 * specific.
 */
void asm_patch_byte(struct util_buffer* p_buf,
                    size_t offset,
                    void* p_start,
                    void* p_patch,
                    uint8_t value);
void asm_patch_u16(struct util_buffer* p_buf,
                   size_t offset,
                   void* p_start,
                   void* p_patch,
                   uint16_t value);
void asm_patch_int(struct util_buffer* p_buf,
                   size_t offset,
                   void* p_start,
                   void* p_patch,
                   int value);
void asm_patch_jump(struct util_buffer* p_buf,
                    size_t offset,
                    void* p_start,
                    void* p_patch,
                    void* p_jump_target);
void asm_copy_patch_byte(struct util_buffer* p_buf,
                         void* p_start,
                         void* p_end,
                         uint8_t value);
void asm_copy_patch_u32(struct util_buffer* p_buf,
                        void* p_start,
                        void* p_end,
                        uint32_t value);

/* asm symbols. */
uint32_t asm_enter_common(void* p_context,
                          uint32_t jump_addr_x64,
                          int64_t countdown,
                          void* p_mem_base);
void asm_debug_trampoline(void);

#endif /* BEEBJIT_ASM_COMMON_H */
