#ifndef BEEBJIT_DISC_HFE_H
#define BEEBJIT_DISC_HFE_H

struct disc_struct;

void disc_hfe_load(struct disc_struct* p_disc);
void disc_hfe_convert(struct disc_struct* p_disc);
void disc_hfe_write_track(struct disc_struct* p_disc);

#endif /* BEEBJIT_DISC_HFE_H */
