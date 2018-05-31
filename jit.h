#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>

struct jit_struct {
  unsigned char* p_mem;   /* 0  */
  void* p_debug_callback; /* 8  */
  unsigned long ip_6502;  /* 16 */
  unsigned char a_6502;   /* 24 */
  unsigned char x_6502;   /* 25 */
  unsigned char y_6502;   /* 26 */
  unsigned char s_6502;   /* 27 */
  unsigned char fz_6502;  /* 28 */
  unsigned char fn_6502;  /* 29 */
  unsigned char fc_6502;  /* 30 */
  unsigned char fo_6502;  /* 31 */
  unsigned char f_6502;   /* 32 */
};

struct jit_struct* jit_create(unsigned char* p_mem);
void jit_destroy(struct jit_struct* p_jit);

void jit_jit(struct jit_struct* p_jit,
             size_t jit_offset,
             size_t jit_len,
             unsigned int flags);
void jit_enter(struct jit_struct* p_jit,
               size_t vector_addr);

#endif /* BEEJIT_JIT_H */
