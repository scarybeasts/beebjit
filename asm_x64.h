#ifndef BEEBJIT_ASM_X64_H
#define BEEBJIT_ASM_X64_H

#include <stddef.h>

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

#endif /* BEEBJIT_ASM_X64_H */
