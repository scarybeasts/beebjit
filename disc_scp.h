#ifndef BEEBJIT_DISC_SCP_H
#define BEEBJIT_DISC_SCP_H

#include <stdint.h>

struct disc_struct;

void disc_scp_load(struct disc_struct* p_disc,
                   uint32_t capture_rev,
                   int quantize_fm,
                   int log_iffy_pulses);

#endif /* BEEBJIT_DISC_SCP_H */
