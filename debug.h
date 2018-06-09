#ifndef BEEBJIT_DEBUG_H
#define BEEBJIT_DEBUG_H

#include <stdint.h>

struct bbc_struct;

struct debug_struct;

struct debug_struct* debug_create(struct bbc_struct* p_bbc);
void debug_destroy(struct debug_struct* p_debug);

void debug_callback(struct debug_struct* p_debug,
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
