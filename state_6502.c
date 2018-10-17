#include "state_6502.h"

void
state_6502_reset(struct state_6502* p_state_6502) {
  /* From https://www.pagetable.com/?p=410, initial 6502 state is not all zero.
   * Also, see http://www.visual6502.org/JSSim/expert.html.
   */
  state_6502_set_registers(p_state_6502,
                           0xAA,
                           0,
                           0,
                           0xFD,
                           /* B, I, Z flags */ 0x16,
                           0);
  state_6502_set_cycles(p_state_6502, 8);
}

void
state_6502_get_registers(struct state_6502* p_state_6502,
                         unsigned char* a,
                         unsigned char* x,
                         unsigned char* y,
                         unsigned char* s,
                         unsigned char* flags,
                         uint16_t* pc) {
  *a = p_state_6502->reg_a;
  *x = p_state_6502->reg_x;
  *y = p_state_6502->reg_y;
  *s = p_state_6502->reg_s;
  *flags = p_state_6502->reg_flags;
  *pc = p_state_6502->reg_pc; 
}

size_t
state_6502_get_cycles(struct state_6502* p_state_6502) {
  return p_state_6502->cycles;
}

void
state_6502_set_registers(struct state_6502* p_state_6502,
                         unsigned char a,
                         unsigned char x,
                         unsigned char y,
                         unsigned char s,
                         unsigned char flags,
                         uint16_t pc) {
  *((unsigned char*) &p_state_6502->reg_a) = a;
  *((unsigned char*) &p_state_6502->reg_x) = x;
  *((unsigned char*) &p_state_6502->reg_y) = y;
  *((unsigned char*) &p_state_6502->reg_s) = s;
  *((unsigned char*) &p_state_6502->reg_flags) = flags;
  state_6502_set_pc(p_state_6502, pc);
}

void
state_6502_set_pc(struct state_6502* p_state_6502, uint16_t pc) {
  *((uint16_t*) &p_state_6502->reg_pc) = pc;
}

void
state_6502_set_cycles(struct state_6502* p_state_6502, size_t cycles) {
  p_state_6502->cycles = cycles;
}
