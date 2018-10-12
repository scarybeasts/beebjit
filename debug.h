#ifndef BEEBJIT_DEBUG_H
#define BEEBJIT_DEBUG_H

#include <stddef.h>
#include <stdint.h>

struct bbc_struct;

struct debug_struct;

struct debug_struct* debug_create(struct bbc_struct* p_bbc,
                                  int debug_active,
                                  uint16_t debug_stop_addr);
void debug_destroy(struct debug_struct* p_debug);

int debug_active_at_addr(struct debug_struct* p_debug, uint16_t addr_6502);
int debug_counter_at_addr(struct debug_struct* p_debug, uint16_t addr_6502);

void* debug_callback(void* p);

#endif /* BEEBJIT_DEBUG_H */
