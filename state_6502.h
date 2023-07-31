#ifndef BEEBJIT_STATE_6502_H
#define BEEBJIT_STATE_6502_H

#include <stdint.h>

enum {
  k_state_6502_irq_via_1 = 1,
  k_state_6502_irq_via_2 = 2,
  k_state_6502_irq_serial_acia = 4,
  k_state_6502_irq_nmi = 8,
};

enum {
  k_state_6502_offset_reg_a =          0,
  k_state_6502_offset_reg_x =          4,
  k_state_6502_offset_reg_y =          8,
  k_state_6502_offset_reg_s =          12,
  k_state_6502_offset_reg_pc =         16,
  k_state_6502_offset_reg_flags =      20,
  k_state_6502_offset_reg_irq_fire =   24,
  k_state_6502_offset_reg_host_pc =    28,
  k_state_6502_offset_reg_host_flags = 32,
  k_state_6502_offset_reg_host_value = 36,
};

struct state_6502 {
  /* Fields in the asm ABI. */
  struct {
    uint32_t reg_a;
    uint32_t reg_x;
    uint32_t reg_y;
    uint32_t reg_s;
    uint32_t reg_pc;
    uint32_t reg_flags;
    uint32_t irq_fire;
    uint32_t reg_host_pc;
    uint32_t reg_host_flags;
    uint32_t reg_host_value;
  } abi_state;

  /* Fields not in the asm ABI. */
  struct timing_struct* p_timing;
  uint32_t timer_id;
  uint8_t* p_mem_read;
  struct {
    uint32_t irq_high;
    uint64_t ticks_baseline;
  } state;
};

struct state_6502* state_6502_create(struct timing_struct* p_timing,
                                     uint8_t* p_mem_read);
void state_6502_destroy(struct state_6502* p_state_6502);

void state_6502_reset(struct state_6502* p_state_6502);

void state_6502_get_registers(struct state_6502* p_state_6502,
                              uint8_t* a,
                              uint8_t* x,
                              uint8_t* y,
                              uint8_t* s,
                              uint8_t* flags,
                              uint16_t* pc);
void state_6502_set_registers(struct state_6502* p_state_6502,
                              uint8_t a,
                              uint8_t x,
                              uint8_t y,
                              uint8_t s,
                              uint8_t flags,
                              uint16_t pc);

uint64_t state_6502_get_cycles(struct state_6502* p_state_6502);
void state_6502_set_cycles(struct state_6502* p_state_6502, uint64_t cycles);

uint16_t state_6502_get_pc(struct state_6502* p_state_6502);
void state_6502_set_pc(struct state_6502* p_state_6502, uint16_t pc);

void state_6502_set_a(struct state_6502* p_state_6502, uint8_t val);
void state_6502_set_x(struct state_6502* p_state_6502, uint8_t val);
void state_6502_set_y(struct state_6502* p_state_6502, uint8_t val);

/* In these calls "IRQ" refers to any "IRQ" or "NMI". */
int state_6502_get_irq_level(struct state_6502* p_state_6502, int irq);
void state_6502_set_irq_level(struct state_6502* p_state_6502,
                              int irq,
                              int level);
int state_6502_check_irq_firing(struct state_6502* p_state_6502, int irq);
int state_6502_check_any_irq_firing(struct state_6502* p_state_6502);

void state_6502_clear_edge_triggered_irq(struct state_6502* p_state_6502,
                                         int irq);

int state_6502_has_irq_high(struct state_6502* p_state_6502);
int state_6502_has_nmi_high(struct state_6502* p_state_6502);

void state_6502_fire_irq_timer(struct state_6502* p_state_6502);

#endif /* BEEBJIT_STATE_6502_H */
