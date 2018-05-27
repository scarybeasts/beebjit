#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>

void jit_init(char* p_mem);
void jit_jit(char* p_mem,
             size_t jit_offset,
             size_t jit_len,
             unsigned int flags);
void jit_enter(char* p_mem, size_t vector_addr);

#endif /* BEEJIT_JIT_H */
