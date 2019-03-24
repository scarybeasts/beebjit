#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct interp_struct;

struct interp_struct* interp_create();

uint32_t interp_enter_with_countdown(struct interp_struct* p_interp,
                                     int64_t countdown);
void interp_set_loop_exit(struct interp_struct* p_interp);

#endif /* BEEBJIT_INTERP_H */
