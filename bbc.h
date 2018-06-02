#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stdint.h>

enum {
  k_bbc_rom_size = 0x4000,
};
enum {
  k_bbc_mode7_width = 40,
  k_bbc_mode7_height = 25,
};

struct bbc_struct;

struct bbc_struct* bbc_create();
void bbc_destroy(struct bbc_struct* p_bbc);

void bbc_reset(struct bbc_struct* p_bbc);
void bbc_run_async(struct bbc_struct* p_bbc);

unsigned char* bbc_get_mem(struct bbc_struct* p_bbc);
unsigned char* bbc_get_mode7_mem(struct bbc_struct* p_bbc);

int bbc_is_special_read_addr(struct bbc_struct* p_bbc, uint16_t addr);
int bbc_is_special_write_addr(struct bbc_struct* p_bbc, uint16_t addr);

/* Callbacks for memory access. */
unsigned char bbc_special_read(struct bbc_struct* p_bbc, uint16_t addr);
void bbc_special_write(struct bbc_struct* p_bbc,
                       uint16_t addr,
                       unsigned char val);

#endif /* BEEJIT_JIT_H */
