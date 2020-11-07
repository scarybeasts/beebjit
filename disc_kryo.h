#ifndef BEEBJIT_DISC_KRYO_H
#define BEEBJIT_DISC_KRYO_H

#include <stdint.h>

struct disc_struct;

void disc_kryo_load(struct disc_struct* p_disc,
                    const char* p_file_name,
                    uint32_t capture_rev,
                    int quantize_fm,
                    int log_iffy_pulses);

#endif /* BEEBJIT_DISC_KRYO_H */
