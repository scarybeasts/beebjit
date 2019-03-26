#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct cpu_driver;
struct cpu_driver_funcs;
struct interp_struct;

struct cpu_driver* interp_create(struct cpu_driver_funcs* p_funcs);

uint32_t interp_enter_with_details(
    struct interp_struct* p_interp,
    int64_t countdown,
    int (*instruction_callback)(uint8_t, int, int));

#endif /* BEEBJIT_INTERP_H */
