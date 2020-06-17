#ifndef BEEBJIT_DISC_HFE_H
#define BEEBJIT_DISC_HFE_H

struct disc_struct;

#include <stdint.h>

void disc_hfe_load(struct disc_struct* p_disc, int expand_to_80);
void disc_hfe_convert(struct disc_struct* p_disc);
void disc_hfe_write_track(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track,
                          uint32_t length,
                          uint8_t* p_data,
                          uint8_t* p_clocks);

#endif /* BEEBJIT_DISC_HFE_H */
