#ifndef BEEBJIT_ASM_JIT_H
#define BEEBJIT_ASM_JIT_H

#include <stdint.h>

struct asm_uop;
struct util_buffer;

struct asm_jit_struct;

int asm_jit_is_enabled(void);
int asm_jit_is_default(void);
void asm_jit_test_preconditions(void);
int asm_jit_supports_uopcode(int32_t uopcode);
int asm_jit_uses_indirect_mappings(void);

struct asm_jit_struct* asm_jit_create(
    void* p_jit_base,
    int (*is_memory_always_ram)(void* p, uint16_t addr),
    void* p_memory_object);
void asm_jit_destroy(struct asm_jit_struct* p_asm);
/* This is stored as the first structure member of the runtime context. */
void* asm_jit_get_private(struct asm_jit_struct* p_asm);

void asm_jit_start_code_updates(struct asm_jit_struct* p_asm,
                                void* p_start,
                                uint32_t length);
void asm_jit_finish_code_updates(struct asm_jit_struct* p_asm);
int asm_jit_handle_fault(struct asm_jit_struct* p_asm,
                         uintptr_t* p_pc,
                         int is_inturbo,
                         int32_t addr_6502,
                         void* p_fault_addr,
                         int is_write);

void asm_jit_invalidate_code_at(void* p);
int asm_jit_is_invalidated_code_at(void* p);

void asm_jit_rewrite(struct asm_jit_struct* p_asm,
                     struct asm_uop* p_uops,
                     uint32_t num_uops);
void asm_emit_jit_invalidated(struct util_buffer* p_buf);
void asm_emit_jit(struct asm_jit_struct* p_asm,
                  struct util_buffer* p_buf,
                  struct util_buffer* p_buf_epilog,
                  struct asm_uop* p_uop);

/* asm symbols. */
uint32_t asm_jit_enter(void* p_context,
                       void* p_start_addr,
                       int64_t countdown,
                       void* p_mem_base);
void asm_jit_interp_trampoline(void);

#endif /* BEEBJIT_ASM_JIT_H */
