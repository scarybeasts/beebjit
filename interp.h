#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct cpu_driver;
struct cpu_driver_funcs;
struct interp_struct;

struct cpu_driver* interp_create(struct cpu_driver_funcs* p_funcs,
                                 int is_65c12);

void interp_set_instruction_callback(
    struct interp_struct* p_interp,
    int (*instruction_callback)(void* p,
                                uint16_t next_pc,
                                uint8_t done_opcode,
                                uint16_t done_addr,
                                int next_is_irq,
                                int irq_pending,
                                int hit_special),
    void* p_callback_context);

int64_t interp_enter_with_countdown(struct interp_struct* p_interp,
                                    int64_t countdown);
int interp_has_memory_written_callback(struct interp_struct* p_interp);

void interp_testing_unexit(struct interp_struct* p_interp);

#endif /* BEEBJIT_INTERP_H */
