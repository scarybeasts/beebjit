#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stddef.h>
#include <stdint.h>

struct via_struct;

enum {
  k_bbc_rom_size = 0x4000,
  k_bbc_ram_size = 0x8000,
};
enum {
  k_bbc_registers_start = 0xFC00,
  k_bbc_registers_len = 0x300,
};
enum {
  k_bbc_mem_mmap_addr = 0x10000000,
  k_bbc_mem_mmap_addr_dummy_rom = 0x11000000,
  k_bbc_mem_mmap_addr_dummy_rom_ro = 0x1100f000,
  k_bbc_mem_mmap_rw_addr = 0x12000000,
};
enum {
  k_bbc_mode_jit = 1,
  k_bbc_mode_interp = 2,
};
enum {
  k_message_exited = 1,
  k_message_vsync = 2,
  k_message_render_done = 3,
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

void bbc_set_mode(struct bbc_struct* p_bbc, int mode);

void bbc_full_reset(struct bbc_struct* p_bbc);
void bbc_get_registers(struct bbc_struct* p_bbc,
                       unsigned char* a,
                       unsigned char* x,
                       unsigned char* y,
                       unsigned char* s,
                       unsigned char* flags,
                       uint16_t* pc);
size_t bbc_get_cycles(struct bbc_struct* p_bbc);
void bbc_set_registers(struct bbc_struct* p_bbc,
                       unsigned char a,
                       unsigned char x,
                       unsigned char y,
                       unsigned char s,
                       unsigned char flags,
                       uint16_t pc);
void bbc_set_pc(struct bbc_struct* p_bbc, uint16_t pc);
void bbc_set_interrupt(struct bbc_struct* p_bbc, int id, int set);
uint16_t bbc_get_block(struct bbc_struct* p_bbc, uint16_t reg_pc);
void bbc_run_async(struct bbc_struct* p_bbc);

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
int bbc_get_vsync_wait_for_render(struct bbc_struct* p_bbc);

void bbc_key_pressed(struct bbc_struct* p_bbc, int key);
void bbc_key_released(struct bbc_struct* p_bbc, int key);
int bbc_is_key_pressed(struct bbc_struct* p_bbc,
                       unsigned char row,
                       unsigned char col);
int bbc_is_key_column_pressed(struct bbc_struct* p_bbc, unsigned char col);
int bbc_is_any_key_pressed(struct bbc_struct* p_bbc);

int bbc_get_client_fd(struct bbc_struct* p_bbc);
void bbc_client_send_message(struct bbc_struct* p_bbc, char message);
char bbc_client_receive_message(struct bbc_struct* p_bbc);

#endif /* BEEJIT_JIT_H */
