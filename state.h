#ifndef BEEBJIT_STATE_H
#define BEEBJIT_STATE_H

#include <stdint.h>

struct bbc_struct;

void state_load(struct bbc_struct* p_bbc, const char* p_file_name);
/* is_irq_next: the CPU has committed to an interrupt at this boundary. */
void state_save(struct bbc_struct* p_bbc,
                const char* p_file_name,
                int is_irq_next);

#endif /* BEEBJIT_STATE_H */
