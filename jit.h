#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>
#include <stdint.h>

struct bbc_struct;
struct debug_struct;

struct jit_struct;

struct jit_struct* jit_create(unsigned char* p_mem,
                              void* p_debug_callback,
                              struct debug_struct* p_debug,
                              struct bbc_struct* p_bbc,
                              void* p_read_callback,
                              void* p_write_callback);
void jit_set_debug(struct jit_struct* p_jit, int debug);
void jit_destroy(struct jit_struct* p_jit);

void jit_get_registers(struct jit_struct* p_jit,
                       unsigned char* a,
                       unsigned char* x,
                       unsigned char* y,
                       unsigned char* s,
                       unsigned char* flags,
                       uint16_t* pc);
void jit_set_registers(struct jit_struct* p_jit,
                       unsigned char a,
                       unsigned char x,
                       unsigned char y,
                       unsigned char s,
                       unsigned char flags,
                       uint16_t pc);

void jit_set_interrupt(struct jit_struct* p_jit, int interrupt);

void jit_enter(struct jit_struct* p_jit);

#endif /* BEEJIT_JIT_H */
