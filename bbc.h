#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stdint.h>

struct bbc_struct;

struct bbc_struct* bbc_create();
void bbc_destroy(struct bbc_struct* p_bbc);

int bbc_is_special_read_addr(struct bbc_struct* p_bbc, uint16_t addr);
int bbc_is_special_write_addr(struct bbc_struct* p_bbc, uint16_t addr);

/* Callbacks for memory access. */
unsigned char bbc_special_read(struct bbc_struct* p_bbc, uint16_t addr);
void bbc_special_write(struct bbc_struct* p_bbc,
                       uint16_t addr,
                       unsigned char val);

#endif /* BEEJIT_JIT_H */
