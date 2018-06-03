#ifndef BEEBJIT_DEBUG_H
#define BEEBJIT_DEBUG_H

#include <stdint.h>

struct jit_struct;

struct debug_struct;

struct debug_struct* debug_create(int run_flag, int print_flag);
void debug_destroy(struct debug_struct* p_debug);

/* TODO: API should be generic, not relying on jit_struct. */
void debug_callback(struct jit_struct* p_jit,
                    uint16_t ip_6502,
                    uint8_t fz_6502,
                    uint8_t fn_6502,
                    uint8_t fc_6502,
                    uint8_t fo_6502,
                    uint8_t f_6502,
                    uint8_t a_6502,
                    uint8_t x_6502,
                    uint8_t y_6502,
                    uint8_t s_6502);

#endif /* BEEBJIT_DEBUG_H */
