#ifndef BEEBJIT_ASM_HELPER_ARM64_H
#define BEEBJIT_ASM_HELPER_ARM64_H

#include <stdint.h>

struct util_buffer;

void asm_copy_patch_arm64_imm12(struct util_buffer* p_buf,
                                void* p_start,
                                void* p_end,
                                uint32_t val);
void asm_patch_arm64_imm12(struct util_buffer* p_buf, uint32_t val);
void asm_copy_patch_arm64_imm16(struct util_buffer* p_buf,
                                void* p_start,
                                void* p_end,
                                uint32_t val);
void asm_patch_arm64_imm16(struct util_buffer* p_buf, uint32_t val);

void asm_copy_patch_arm64_imm14_pc_rel(struct util_buffer* p_buf,
                                       void* p_start,
                                       void* p_end,
                                       void* p_target);
void asm_patch_arm64_imm14_pc_rel(struct util_buffer* p_buf, void* p_target);
void asm_copy_patch_arm64_imm19_pc_rel(struct util_buffer* p_buf,
                                       void* p_start,
                                       void* p_end,
                                       void* p_target);
void asm_patch_arm64_imm19_pc_rel(struct util_buffer* p_buf, void* p_target);
void asm_copy_patch_arm64_imm26_pc_rel(struct util_buffer* p_buf,
                                       void* p_start,
                                       void* p_end,
                                       void* p_target);
void asm_patch_arm64_imm26_pc_rel(struct util_buffer* p_buf, void* p_target);

int asm_calculate_immr_imms(uint8_t* p_immr, uint8_t* p_imms, uint8_t val);

#endif /* BEEBJIT_ASM_HELPER_ARM64_H */
