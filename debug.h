#ifndef BEEBJIT_DEBUG_H
#define BEEBJIT_DEBUG_H

struct jit_struct;

struct debug_struct;

struct debug_struct* debug_create(int run_flag, int print_flag);
void debug_destroy(struct debug_struct* p_debug);

/* TODO: API should be generic, not relying on jit_struct. */
void debug_callback(struct jit_struct* p_jit);

#endif /* BEEBJIT_DEBUG_H */
