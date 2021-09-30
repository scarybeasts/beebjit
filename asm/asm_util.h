#ifndef ASM_UTIL_H
#define ASM_UTIL_H

#include <stdint.h>

struct asm_uop;

void asm_make_uop0(struct asm_uop* p_uop, int32_t uopcode);
void asm_make_uop1(struct asm_uop* p_uop, int32_t uopcode, int32_t val1);
struct asm_uop* asm_find_uop(int32_t* p_out_index,
                             struct asm_uop* p_uops,
                             uint32_t num_uops,
                             int32_t uopcode);
struct asm_uop* asm_insert_uop(struct asm_uop* p_uops,
                               uint32_t num_uops,
                               uint32_t index);

void asm_breakdown_from_6502(struct asm_uop* p_uops,
                             uint32_t num_uops,
                             struct asm_uop** p_out_main,
                             struct asm_uop** p_out_mode,
                             struct asm_uop** p_out_load,
                             struct asm_uop** p_out_store,
                             struct asm_uop** p_out_load_carry,
                             struct asm_uop** p_out_save_carry,
                             struct asm_uop** p_out_nz_flags,
                             struct asm_uop** p_out_inv,
                             struct asm_uop** p_out_addr_check,
                             struct asm_uop** p_out_page_crossing);

#endif /* ASM_UTIL_H */
