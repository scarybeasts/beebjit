#ifndef BEEBJIT_DISC_RFI_H
#define BEEBJIT_DISC_RFI_H

#include <stdint.h>

struct disc_struct;

void disc_rfi_load(struct disc_struct* p_disc,
                   uint32_t rev,
                   char* p_rev_spec,
                   int log_iffy_pulses);

#endif /* BEEBJIT_DISC_RFI_H */
