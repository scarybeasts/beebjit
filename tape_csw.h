#ifndef BEEBJIT_TAPE_CSW_H
#define BEEBJIT_TAPE_CSW_H

#include <stdint.h>

struct tape_struct;

void tape_csw_load(struct tape_struct* p_tape,
                   uint8_t* p_src,
                   uint32_t src_len,
                   int do_check_bits);

#endif /* BEEBJIT_TAPE_CSW_H */
