#ifndef BEEBJIT_STATE_6502_H
#define BEEBJIT_STATE_6502_H

#include <stdint.h>

enum {
  k_6502_stack_addr = 0x100,
};

struct state_6502 {
  unsigned int reg_a;
  unsigned int reg_x;
  unsigned int reg_y;
  unsigned int reg_s;
  unsigned int reg_pc;
  unsigned int reg_flags;
  unsigned int irq;
  unsigned int reg_host_pc;
  unsigned int reg_host_flags;
};

void state_6502_get_registers(struct state_6502* p_state_6502,
                              unsigned char* a,
                              unsigned char* x,
                              unsigned char* y,
                              unsigned char* s,
                              unsigned char* flags,
                              uint16_t* pc);

void state_6502_set_registers(struct state_6502* p_state_6502,
                              unsigned char a,
                              unsigned char x,
                              unsigned char y,
                              unsigned char s,
                              unsigned char flags,
                              uint16_t pc);

#endif /* BEEBJIT_STATE_6502_H */
