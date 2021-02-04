#ifndef BEEBJIT_DISC_DFI_H
#define BEEBJIT_DISC_DFI_H

#include <stdint.h>

struct disc_struct;

void disc_dfi_load(struct disc_struct* p_disc,
                   uint32_t capture_rev,
                   int quantize_fm,
                   int log_iffy_pulses,
                   int is_skip_odd_tracks,
                   int is_skip_upper_side);

#endif /* BEEBJIT_DISC_DFI_H */
