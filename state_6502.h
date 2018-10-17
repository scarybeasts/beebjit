#ifndef BEEBJIT_STATE_6502_H
#define BEEBJIT_STATE_6502_H

#include <stddef.h>
#include <stdint.h>

enum {
  k_state_6502_offset_reg_a =          0,
  k_state_6502_offset_reg_x =          4,
  k_state_6502_offset_reg_y =          8,
  k_state_6502_offset_reg_s =          12,
  k_state_6502_offset_reg_pc =         16,
  k_state_6502_offset_reg_flags =      20,
  k_state_6502_offset_reg_irq =        24,
  k_state_6502_offset_reg_host_pc =    28,
  k_state_6502_offset_reg_host_flags = 32,
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
  size_t cycles;
};

void state_6502_reset(struct state_6502* p_state_6502);

void state_6502_get_registers(struct state_6502* p_state_6502,
                              unsigned char* a,
                              unsigned char* x,
                              unsigned char* y,
                              unsigned char* s,
                              unsigned char* flags,
                              uint16_t* pc);
size_t state_6502_get_cycles(struct state_6502* p_state_6502);

void state_6502_set_registers(struct state_6502* p_state_6502,
                              unsigned char a,
                              unsigned char x,
                              unsigned char y,
                              unsigned char s,
                              unsigned char flags,
                              uint16_t pc);
void state_6502_set_pc(struct state_6502* p_state_6502, uint16_t pc);
void state_6502_set_cycles(struct state_6502* p_state_6502, size_t cycles);

#endif /* BEEBJIT_STATE_6502_H */
