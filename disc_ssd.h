#ifndef BEEBJIT_DISC_SSD_H
#define BEEBJIT_DISC_SSD_H

struct disc_struct;

void disc_ssd_load(struct disc_struct* p_disc, int is_dsd);
void disc_ssd_write_track(struct disc_struct* p_disc);

#endif /* BEEBJIT_DISC_SSD_H */
