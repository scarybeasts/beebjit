#ifndef BEEBJIT_DISC_RFI_H
#define BEEBJIT_DISC_RFI_H

struct disc_struct;

#include <stdint.h>

#define RFI_MAGIC "RFI"
#define USINSECOND 1000000

void disc_rfi_load(struct disc_struct* p_disc);

#endif /* BEEBJIT_DISC_RFI_H */
