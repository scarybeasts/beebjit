#include "state_6502.h"

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
  *((uint16_t*) &p_state_6502->reg_pc) = pc;
}
