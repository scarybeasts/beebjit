#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct interp_struct;

struct interp_struct* interp_create();

uint32_t interp_enter_with_details(
    struct interp_struct* p_interp,
    int64_t countdown,
    int (*instruction_callback)(uint8_t, int, int));

#endif /* BEEBJIT_INTERP_H */
