#ifndef BEEBJIT_STATE_H
#define BEEBJIT_STATE_H

#include <stdint.h>

struct bbc_struct;

void state_load(struct bbc_struct* p_bbc, const char* p_file_name);
void state_save(struct bbc_struct* p_bbc, const char* p_file_name);

#endif /* BEEBJIT_STATE_H */
