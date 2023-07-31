#include "state_6502.h"

#include "defs_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <string.h>

static void
state_6502_timer_fired(void* p) {
  struct state_6502* p_state_6502 = (struct state_6502*) p;
  (void) timing_stop_timer(p_state_6502->p_timing, p_state_6502->timer_id);
}

struct state_6502*
state_6502_create(struct timing_struct* p_timing, uint8_t* p_mem_read) {
  struct state_6502* p_state_6502 = util_mallocz(sizeof(struct state_6502));

  p_state_6502->p_timing = p_timing;
  p_state_6502->p_mem_read = p_mem_read;

  p_state_6502->timer_id = timing_register_timer(p_timing,
                                                 "6502_irq",
                                                 state_6502_timer_fired,
                                                 p_state_6502);

  return p_state_6502;
}

void
state_6502_destroy(struct state_6502* p_state_6502) {
  util_free(p_state_6502);
}

void
state_6502_reset(struct state_6502* p_state_6502) {
  uint16_t init_pc;
  uint32_t reg_s;

  uint8_t* p_mem_read = p_state_6502->p_mem_read;

  /* Preserve the upper bits of S, in case the asm backend uses a pointer value
   * there.
   */
  reg_s = p_state_6502->abi_state.reg_s;

  (void) memset(&p_state_6502->abi_state,
                '\0',
                sizeof(p_state_6502->abi_state));
  (void) memset(&p_state_6502->state, '\0', sizeof(p_state_6502->state));

  p_state_6502->abi_state.reg_s = reg_s;

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
  /* NOTE: this isn't strictly correct to jump straight to 8 cycles, as it
   * does not advance time. There should probably be an actual RESET opcode
   * in the interpreter that ticks out these 8 cycles correctly.
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
  *a = p_state_6502->abi_state.reg_a;
  *x = p_state_6502->abi_state.reg_x;
  *y = p_state_6502->abi_state.reg_y;
  *s = p_state_6502->abi_state.reg_s;
  *flags = p_state_6502->abi_state.reg_flags;
  *pc = p_state_6502->abi_state.reg_pc; 
}

void
state_6502_set_registers(struct state_6502* p_state_6502,
                         uint8_t a,
                         uint8_t x,
                         uint8_t y,
                         uint8_t s,
                         uint8_t flags,
                         uint16_t pc) {
  *((uint8_t*) &p_state_6502->abi_state.reg_a) = a;
  *((uint8_t*) &p_state_6502->abi_state.reg_x) = x;
  *((uint8_t*) &p_state_6502->abi_state.reg_y) = y;
  *((uint8_t*) &p_state_6502->abi_state.reg_s) = s;
  *((uint8_t*) &p_state_6502->abi_state.reg_flags) = flags;
  state_6502_set_pc(p_state_6502, pc);
}

uint64_t
state_6502_get_cycles(struct state_6502* p_state_6502) {
  uint64_t timer_ticks = timing_get_total_timer_ticks(p_state_6502->p_timing);

  return (timer_ticks - p_state_6502->state.ticks_baseline);
}

void
state_6502_set_cycles(struct state_6502* p_state_6502, uint64_t cycles) {
  uint64_t timer_ticks = timing_get_total_timer_ticks(p_state_6502->p_timing);
  p_state_6502->state.ticks_baseline = (timer_ticks - cycles);
}

uint16_t
state_6502_get_pc(struct state_6502* p_state_6502) {
  uint32_t* p_pc = &p_state_6502->abi_state.reg_pc;
  return *((uint16_t*) p_pc);
}

void
state_6502_set_pc(struct state_6502* p_state_6502, uint16_t pc) {
  uint32_t* p_pc = &p_state_6502->abi_state.reg_pc;
  *((uint16_t*) p_pc) = pc;
}

void
state_6502_set_a(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_a = &p_state_6502->abi_state.reg_a;
  *((uint8_t*) p_a) = val;
}

void
state_6502_set_x(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_x = &p_state_6502->abi_state.reg_x;
  *((uint8_t*) p_x) = val;
}

void
state_6502_set_y(struct state_6502* p_state_6502, uint8_t val) {
  uint32_t* p_y = &p_state_6502->abi_state.reg_y;
  *((uint8_t*) p_y) = val;
}

static int
state_6502_irq_is_edge_triggered(int irq) {
  if (irq == k_state_6502_irq_nmi) {
    return 1;
  }
  return 0;
}

int
state_6502_get_irq_level(struct state_6502* p_state_6502, int irq) {
  return !!(p_state_6502->state.irq_high & irq);
}

void
state_6502_set_irq_level(struct state_6502* p_state_6502, int irq, int level) {
  int old_level = !!(p_state_6502->state.irq_high & irq);

  if (level) {
    p_state_6502->state.irq_high |= irq;
  } else {
    p_state_6502->state.irq_high &= ~irq;
  }

  if (state_6502_irq_is_edge_triggered(irq)) {
    if (level && !old_level) {
      p_state_6502->abi_state.irq_fire |= irq;
    }
  } else {
    if (level) {
      p_state_6502->abi_state.irq_fire |= irq;
    } else {
      p_state_6502->abi_state.irq_fire &= ~irq;
    }
  }
}

int
state_6502_check_irq_firing(struct state_6502* p_state_6502, int irq) {
  return !!(p_state_6502->abi_state.irq_fire & irq);
}

int
state_6502_check_any_irq_firing(struct state_6502* p_state_6502) {
  return !!p_state_6502->abi_state.irq_fire;
}

void
state_6502_clear_edge_triggered_irq(struct state_6502* p_state_6502, int irq) {
  int fire = !!(p_state_6502->abi_state.irq_fire & irq);

  (void) fire;

  assert(state_6502_irq_is_edge_triggered(irq));
  assert(fire);

  p_state_6502->abi_state.irq_fire &= ~irq;
}

int
state_6502_has_irq_high(struct state_6502* p_state_6502) {
  return !!(p_state_6502->state.irq_high & ~k_state_6502_irq_nmi);
}

int
state_6502_has_nmi_high(struct state_6502* p_state_6502) {
  return !!(p_state_6502->state.irq_high & k_state_6502_irq_nmi);
}

void
state_6502_fire_irq_timer(struct state_6502* p_state_6502) {
  assert(!timing_timer_is_running(p_state_6502->p_timing,
                                  p_state_6502->timer_id));
  (void) timing_start_timer_with_value(p_state_6502->p_timing,
                                       p_state_6502->timer_id,
                                       0);
}
