#ifndef BEEBJIT_JIT_COMPILER_H
#define BEEBJIT_JIT_COMPILER_H

#include <stdint.h>

struct jit_compiler;
struct util_buffer;

struct jit_compiler* jit_compiler_create(
    uint8_t* p_read_mem,
    void* (*host_address_resolver)(void*, uint16_t),
    void* p_host_address_object,
    uint32_t* p_jit_ptrs,
    int debug);
void jit_compiler_destroy(struct jit_compiler* p_compiler);

void jit_compiler_compile_block(struct jit_compiler* p_compiler,
                                struct util_buffer* p_buf,
                                uint16_t addr_6502);

#endif /* BEEJIT_JIT_COMPILER_H */
