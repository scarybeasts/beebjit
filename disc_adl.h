#ifndef BEEBJIT_DISC_ADL_H
#define BEEBJIT_DISC_ADL_H

struct disc_struct;

#include <stdint.h>

void disc_adl_load(struct disc_struct* p_disc);
void disc_adl_write_track(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track,
                          uint32_t length,
                          uint32_t* p_pulses);

#endif /* BEEBJIT_DISC_ADL_H */
