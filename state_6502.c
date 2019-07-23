#include "state_6502.h"

#include "defs_6502.h"
#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct state_6502*
state_6502_create(struct timing_struct* p_timing, uint8_t* p_mem_read) {
  struct state_6502* p_state_6502 = malloc(sizeof(struct state_6502));
  if (p_state_6502 == NULL) {
    errx(1, "couldn't allocate state_6502");
  }

  (void) memset(p_state_6502, '\0', sizeof(struct state_6502));

  p_state_6502->reset_pending = 0;
  p_state_6502->p_mem_read = p_mem_read;
  p_state_6502->p_timing = p_timing;
  p_state_6502->ticks_baseline = 0;

  return p_state_6502;
}

void
state_6502_destroy(struct state_6502* p_state_6502) {
  free(p_state_6502);
}

void
state_6502_reset(struct state_6502* p_state_6502) {
  uint16_t init_pc;

  uint8_t* p_mem_read = p_state_6502->p_mem_read;

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

  /* A 6502 takes 8 cycles to reset. First instruction, the opcode at the
   * reset vector, commences thereafter.
   */
  state_6502_set_cycles(p_state_6502, 8);

  init_pc = (p_mem_read[k_6502_vector_reset] |
             (p_mem_read[k_6502_vector_reset + 1] << 8));
  state_6502_set_pc(p_state_6502, init_pc);
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
  uint64_t timer_ticks = timing_get_total_timer_ticks(p_state_6502->p_timing);

  return (timer_ticks - p_state_6502->ticks_baseline);
}

void
state_6502_set_cycles(struct state_6502* p_state_6502, uint64_t cycles) {
  uint64_t timer_ticks = timing_get_total_timer_ticks(p_state_6502->p_timing);
  p_state_6502->ticks_baseline = (timer_ticks - cycles);
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

void
state_6502_set_a(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_a = &p_state_6502->reg_a;
  *((uint8_t*) p_a) = val;
}

void
state_6502_set_x(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_x = &p_state_6502->reg_x;
  *((uint8_t*) p_x) = val;
}

void
state_6502_set_y(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_y = &p_state_6502->reg_y;
  *((uint8_t*) p_y) = val;
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

  if (level) {
    p_state_6502->irq_high |= irq_value;
  } else {
    p_state_6502->irq_high &= ~irq_value;
  }

  if (state_6502_irq_is_edge_triggered(irq)) {
    if (level && !old_level) {
      p_state_6502->irq_fire |= irq_value;
    }
  } else {
    if (level) {
      p_state_6502->irq_fire |= irq_value;
    } else {
      p_state_6502->irq_fire &= ~irq_value;
    }
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

void
state_6502_set_reset_pending(struct state_6502* p_state_6502) {
  p_state_6502->reset_pending = 1;
}

int
state_6502_check_and_do_reset(struct state_6502* p_state_6502) {
  int ret = p_state_6502->reset_pending;

  p_state_6502->reset_pending = 0;
  if (ret) {
    state_6502_reset(p_state_6502);
  }
  return ret;
}
