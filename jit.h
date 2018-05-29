#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>

struct jit_struct;

struct jit_struct* jit_create(unsigned char* p_mem);
void jit_destroy(struct jit_struct* p_jit);

void jit_jit(struct jit_struct* p_jit,
             size_t jit_offset,
             size_t jit_len,
             unsigned int flags);
void jit_enter(struct jit_struct* p_jit,
               size_t vector_addr);

#endif /* BEEJIT_JIT_H */
