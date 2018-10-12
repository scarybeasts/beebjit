#ifndef BEEBJIT_STATE_6502_H
#define BEEBJIT_STATE_6502_H

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

#endif /* BEEBJIT_STATE_6502_H */
