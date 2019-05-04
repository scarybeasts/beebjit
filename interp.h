#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct cpu_driver;
struct cpu_driver_funcs;
struct interp_struct;

struct cpu_driver* interp_create(struct cpu_driver_funcs* p_funcs);

int interp_enter_with_details(
    struct interp_struct* p_interp,
    int64_t countdown,
    int (*instruction_callback)(void* p,
                                uint16_t next_pc,
                                uint8_t done_opcode,
                                uint16_t done_addr,
                                int next_is_irq,
                                int irq_pending),
    void* p_callback_context);

#endif /* BEEBJIT_INTERP_H */
