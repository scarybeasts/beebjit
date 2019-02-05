#ifndef BEEBJIT_INTURBO_H
#define BEEBJIT_INTURBO_H

#include <stdint.h>

struct bbc_options;
struct interp_struct;
struct inturbo_struct;
struct memory_access;
struct state_6502;
struct timing_struct;

struct inturbo_struct* inturbo_create(struct state_6502* p_state_6502,
                                      struct memory_access* p_memory_access,
                                      struct timing_struct* p_timing,
                                      struct bbc_options* p_options,
                                      struct interp_struct* p_interp);
void inturbo_destroy(struct inturbo_struct* p_inturbo);

uint32_t inturbo_enter(struct inturbo_struct* p_inturbo);

#endif /* BEEBJIT_INTURBO_H */
