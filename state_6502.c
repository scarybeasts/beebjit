#include "state_6502.h"

#include <assert.h>

void
state_6502_reset(struct state_6502* p_state_6502) {
  /* EMU: from https://www.pagetable.com/?p=410, initial 6502 state is not all
   * zero. Also, see http://www.visual6502.org/JSSim/expert.html.
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
                         uint8_t* a,
                         uint8_t* x,
                         uint8_t* y,
                         uint8_t* s,
                         uint8_t* flags,
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
                         uint8_t a,
                         uint8_t x,
                         uint8_t y,
                         uint8_t s,
                         uint8_t flags,
                         uint16_t pc) {
  *((uint8_t*) &p_state_6502->reg_a) = a;
  *((uint8_t*) &p_state_6502->reg_x) = x;
  *((uint8_t*) &p_state_6502->reg_y) = y;
  *((uint8_t*) &p_state_6502->reg_s) = s;
  *((uint8_t*) &p_state_6502->reg_flags) = flags;
  state_6502_set_pc(p_state_6502, pc);
}

uint64_t
state_6502_get_cycles(struct state_6502* p_state_6502) {
  return p_state_6502->cycles;
}

void
state_6502_set_cycles(struct state_6502* p_state_6502, uint64_t cycles) {
  p_state_6502->cycles = cycles;
}

void
state_6502_add_cycles(struct state_6502* p_state_6502, uint64_t cycles) {
  p_state_6502->cycles += cycles;
}

uint16_t
state_6502_get_pc(struct state_6502* p_state_6502) {
  uint32_t* p_pc = &p_state_6502->reg_pc;
  return *((uint16_t*) p_pc);
}

void
state_6502_set_pc(struct state_6502* p_state_6502, uint16_t pc) {
  uint32_t* p_pc = &p_state_6502->reg_pc;
  *((uint16_t*) p_pc) = pc;
}

static int
state_6502_irq_is_edge_triggered(int irq) {
  if (irq == k_state_6502_irq_nmi) {
    return 1;
  }
  return 0;
}

void
state_6502_set_irq_level(struct state_6502* p_state_6502, int irq, int level) {
  int irq_value = (1 << irq);
  int old_level = !!(p_state_6502->irq_high & irq_value);
  int fire;

  if (level) {
    p_state_6502->irq_high |= irq_value;
  } else {
    p_state_6502->irq_high &= ~irq_value;
  }

  if (state_6502_irq_is_edge_triggered(irq)) {
    if (level && !old_level) {
      fire = 1;
    } else {
      fire = 0;
    }
  } else {
    fire = level;
  }

  if (fire) {
    p_state_6502->irq_fire |= irq_value;
  } else {
    p_state_6502->irq_fire &= ~irq_value;
  }
}

int
state_6502_check_irq_firing(struct state_6502* p_state_6502, int irq) {
  int irq_value = (1 << irq);
  int fire = !!(p_state_6502->irq_fire & irq_value);

  return fire;
}

void
state_6502_clear_edge_triggered_irq(struct state_6502* p_state_6502, int irq) {
  int irq_value = (1 << irq);
  int fire = !!(p_state_6502->irq_fire & irq_value);

  (void) fire;

  assert(state_6502_irq_is_edge_triggered(irq));
  assert(fire);

  p_state_6502->irq_fire &= ~irq_value;
}
