#ifndef BEEBJIT_JIT_COMPILER_H
#define BEEBJIT_JIT_COMPILER_H

#include <stdint.h>

struct bbc_options;
struct jit_compiler;
struct memory_access;
struct state_6502;
struct util_buffer;

struct jit_compiler* jit_compiler_create(
    struct memory_access* p_memory_access,
    void* (*get_block_host_address)(void* p, uint16_t addr),
    void* (*get_trampoline_host_address)(void* p, uint16_t addr),
    uint16_t (*get_jit_ptr_block)(void* p, uint32_t jit_ptr),
    void* p_host_address_object,
    uint32_t* p_jit_ptrs,
    struct bbc_options* p_options,
    int debug,
    int option_accurate_timings);
void jit_compiler_destroy(struct jit_compiler* p_compiler);

void jit_compiler_compile_block(struct jit_compiler* p_compiler,
                                struct util_buffer* p_buf,
                                uint16_t addr_6502);

int64_t jit_compiler_fixup_state(struct jit_compiler* p_compiler,
                                 struct state_6502* p_state_6502,
                                 int64_t countdown);

void jit_compiler_memory_range_invalidate(struct jit_compiler* p_compiler,
                                          uint16_t addr,
                                          uint16_t len);

int jit_compiler_is_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler);
void jit_compiler_set_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler, int value);

#endif /* BEEJIT_JIT_COMPILER_H */
