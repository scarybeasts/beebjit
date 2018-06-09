#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>

struct bbc_struct;
struct debug_struct;

struct jit_struct;

struct jit_struct* jit_create(unsigned char* p_mem,
                              void* p_debug_callback,
                              struct debug_struct* p_debug,
                              struct bbc_struct* p_bbc,
                              void* p_read_callback,
                              void* p_write_callback);
void jit_destroy(struct jit_struct* p_jit);

void jit_set_interrupt(struct jit_struct* p_jit, int interrupt);

void jit_jit(struct jit_struct* p_jit,
             size_t jit_offset,
             size_t jit_len,
             unsigned int flags);
void jit_enter(struct jit_struct* p_jit,
               size_t vector_addr);

#endif /* BEEJIT_JIT_H */
