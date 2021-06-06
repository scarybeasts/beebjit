#ifndef BEEBJIT_TAPE_UEF_H
#define BEEBJIT_TAPE_UEF_H

#include <stdint.h>

struct tape_struct;

void tape_uef_load(struct tape_struct* p_tape,
                   uint8_t* p_src,
                   uint32_t src_len);

#endif /* BEEBJIT_TAPE_UEF_H */
