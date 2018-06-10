#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stdint.h>

enum {
  k_bbc_rom_size = 0x4000,
};
enum {
  k_bbc_vector_reset = 0xfffc,
  k_bbc_vector_irq = 0xfffe,
};
enum {
  k_bbc_mode7_width = 40,
  k_bbc_mode7_height = 25,
};

struct bbc_struct;

struct bbc_struct* bbc_create(unsigned char* p_os_rom,
                              unsigned char* p_lang_rom,
                              int debug_flag,
                              int run_flag,
                              int print_flag);
void bbc_destroy(struct bbc_struct* p_bbc);

void bbc_reset(struct bbc_struct* p_bbc);
void bbc_run_async(struct bbc_struct* p_bbc);

void bbc_fire_interrupt(struct bbc_struct* p_bbc, int user, unsigned char bits);

unsigned char* bbc_get_mem(struct bbc_struct* p_bbc);
unsigned char* bbc_get_mode7_mem(struct bbc_struct* p_bbc);
int bbc_get_run_flag(struct bbc_struct* p_bbc);
int bbc_get_print_flag(struct bbc_struct* p_bbc);

int bbc_is_special_read_addr(struct bbc_struct* p_bbc, uint16_t addr);
int bbc_is_special_write_addr(struct bbc_struct* p_bbc, uint16_t addr);

/* Callbacks for memory access. */
unsigned char bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr);
void bbc_write_callback(struct bbc_struct* p_bbc, uint16_t addr);

void bbc_key_pressed(struct bbc_struct* p_bbc, int key);
void bbc_key_released(struct bbc_struct* p_bbc, int key);

#endif /* BEEJIT_JIT_H */
