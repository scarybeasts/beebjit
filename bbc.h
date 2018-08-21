#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stddef.h>
#include <stdint.h>

enum {
  k_bbc_addr_space_size = 0x10000,
  k_bbc_rom_size = 0x4000,
  k_bbc_ram_size = 0x8000,
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
uint16_t bbc_get_block(struct bbc_struct* p_bbc, uint16_t reg_pc);
void bbc_check_pc(struct bbc_struct* p_bbc);
void bbc_run_async(struct bbc_struct* p_bbc);

void bbc_get_sysvia(struct bbc_struct* p_bbc,
                    unsigned char* sysvia_ORA,
                    unsigned char* sysvia_ORB,
                    unsigned char* sysvia_DDRA,
                    unsigned char* sysvia_DDRB,
                    unsigned char* sysvia_SR,
                    unsigned char* sysvia_ACR,
                    unsigned char* sysvia_PCR,
                    unsigned char* sysvia_IFR,
                    unsigned char* sysvia_IER,
                    unsigned char* sysvia_IC32);
void bbc_set_sysvia(struct bbc_struct* p_bbc,
                    unsigned char sysvia_ORA,
                    unsigned char sysvia_ORB,
                    unsigned char sysvia_DDRA,
                    unsigned char sysvia_DDRB,
                    unsigned char sysvia_SR,
                    unsigned char sysvia_ACR,
                    unsigned char sysvia_PCR,
                    unsigned char sysvia_IFR,
                    unsigned char sysvia_IER,
                    unsigned char sysvia_IC32);
unsigned char bbc_get_video_ula(struct bbc_struct* p_bbc);
void bbc_set_video_ula(struct bbc_struct* p_bbc, unsigned char ula_control);

void bbc_fire_interrupt(struct bbc_struct* p_bbc, int user, unsigned char bits);

unsigned char* bbc_get_mem(struct bbc_struct* p_bbc);
void bbc_set_memory_block(struct bbc_struct* p_bbc,
                          uint16_t addr,
                          uint16_t len,
                          unsigned char* p_src_mem);

int bbc_get_run_flag(struct bbc_struct* p_bbc);
int bbc_get_print_flag(struct bbc_struct* p_bbc);

unsigned char* bbc_get_screen_mem(struct bbc_struct* p_bbc);
int bbc_get_screen_is_text(struct bbc_struct* p_bbc);
size_t bbc_get_screen_pixel_width(struct bbc_struct* p_bbc);
size_t bbc_get_screen_clock_speed(struct bbc_struct* p_bbc);

int bbc_is_special_address(struct bbc_struct* p_bbc, uint16_t addr);

/* Callbacks for memory access. */
unsigned char bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr);
void bbc_write_callback(struct bbc_struct* p_bbc,
                        uint16_t addr,
                        unsigned char val);

void bbc_key_pressed(struct bbc_struct* p_bbc, int key);
void bbc_key_released(struct bbc_struct* p_bbc, int key);

#endif /* BEEJIT_JIT_H */
