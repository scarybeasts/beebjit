#ifndef BEEBJIT_DISC_FSD_H
#define BEEBJIT_DISC_FSD_H

#include <stdint.h>

struct disc_struct;

void disc_fsd_load(struct disc_struct* p_disc,
                   intptr_t file_handle,
                   int log_protection);

#endif /* BEEBJIT_DISC_FSD_H */
