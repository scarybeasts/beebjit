#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stddef.h>
#include <stdint.h>

struct via_struct;

enum {
  k_bbc_addr_space_size = 0x10000,
  k_bbc_rom_size = 0x4000,
  k_bbc_ram_size = 0x8000,
};
enum {
  k_bbc_registers_start = 0xfc00,
  k_bbc_registers_len = 0x300,
  k_bbc_vector_reset = 0xfffc,
  k_bbc_vector_irq = 0xfffe,
};
enum {
  k_bbc_mem_mmap_addr = 0x10000000,
  k_bbc_mem_mmap_addr_dummy_rom = 0x11000000,
  k_bbc_mem_mmap_addr_dummy_rom_ro = 0x1100f000,
};

struct bbc_struct;

struct bbc_struct* bbc_create(unsigned char* p_os_rom,
                              unsigned char* p_lang_rom,
                              int debug_flag,
                              int run_flag,
                              int print_flag,
                              int slow_flag,
                              const char* p_opt_flags,
                              const char* p_log_flags,
                              uint16_t debug_stop_addr);
void bbc_destroy(struct bbc_struct* p_bbc);

void bbc_reset(struct bbc_struct* p_bbc);
void bbc_get_registers(struct bbc_struct* p_bbc,
                       unsigned char* a,
                       unsigned char* x,
                       unsigned char* y,
                       unsigned char* s,
                       unsigned char* flags,
                       uint16_t* pc);
void bbc_set_registers(struct bbc_struct* p_bbc,
                       unsigned char a,
                       unsigned char x,
                       unsigned char y,
                       unsigned char s,
                       unsigned char flags,
                       uint16_t pc);
void bbc_set_interrupt(struct bbc_struct* p_bbc, int id, int set);
uint16_t bbc_get_block(struct bbc_struct* p_bbc, uint16_t reg_pc);
void bbc_check_pc(struct bbc_struct* p_bbc);
void bbc_run_async(struct bbc_struct* p_bbc);
int bbc_has_exited(struct bbc_struct* p_bbc);
void bbc_sync_timer_tick(struct bbc_struct* p_bbc);

struct via_struct* bbc_get_sysvia(struct bbc_struct* p_bbc);
struct via_struct* bbc_get_uservia(struct bbc_struct* p_bbc);

struct jit_struct* bbc_get_jit(struct bbc_struct* p_bbc);
struct video_struct* bbc_get_video(struct bbc_struct* p_bbc);
unsigned char* bbc_get_mem(struct bbc_struct* p_bbc);
void bbc_set_memory_block(struct bbc_struct* p_bbc,
                          uint16_t addr,
                          uint16_t len,
                          unsigned char* p_src_mem);
void bbc_memory_write(struct bbc_struct* p_bbc,
                      uint16_t addr_6502,
                      unsigned char val);

int bbc_get_run_flag(struct bbc_struct* p_bbc);
int bbc_get_print_flag(struct bbc_struct* p_bbc);
int bbc_get_slow_flag(struct bbc_struct* p_bbc);

int bbc_is_ram_address(struct bbc_struct* p_bbc, uint16_t addr);
int bbc_is_special_read_address(struct bbc_struct* p_bbc,
                                uint16_t addr_low,
                                uint16_t addr_high);
int bbc_is_special_write_address(struct bbc_struct* p_bbc,
                                 uint16_t addr_low,
                                 uint16_t addr_high);

/* Callbacks for memory access. */
unsigned char bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr);
void bbc_write_callback(struct bbc_struct* p_bbc,
                        uint16_t addr,
                        unsigned char val);

void bbc_key_pressed(struct bbc_struct* p_bbc, int key);
void bbc_key_released(struct bbc_struct* p_bbc, int key);
int bbc_is_key_pressed(struct bbc_struct* p_bbc,
                       unsigned char row,
                       unsigned char col);
int bbc_is_key_column_pressed(struct bbc_struct* p_bbc, unsigned char col);
int bbc_is_any_key_pressed(struct bbc_struct* p_bbc);

int bbc_get_fd(struct bbc_struct* p_bbc);

#endif /* BEEJIT_JIT_H */
