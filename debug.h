#ifndef BEEBJIT_DEBUG_H
#define BEEBJIT_DEBUG_H

#include <stdint.h>

struct bbc_struct;

struct debug_struct;

struct debug_struct* debug_create(struct bbc_struct* p_bbc);
void debug_destroy(struct debug_struct* p_debug);

void debug_callback(struct debug_struct* p_debug);

#endif /* BEEBJIT_DEBUG_H */
