#ifndef BEEBJIT_BBC_H
#define BEEBJIT_BBC_H

#include <stddef.h>
#include <stdint.h>

struct cmos_struct;
struct cpu_driver;
struct keyboard_struct;
struct serial_struct;
struct sound_struct;
struct state_6502;
struct via_struct;
struct video_struct;

enum {
  k_bbc_rom_size = 0x4000,
  k_bbc_ram_size = 0x8000,
};
enum {
  k_bbc_num_roms = 16,
  k_bbc_default_dfs_rom_slot = 0xD,
  k_bbc_default_basic_rom_slot = 0xC,
};
enum {
  k_bbc_registers_start = 0xFC00,
  k_bbc_registers_len = 0x300,
};
enum {
  k_message_exited = 1,
  k_message_vsync = 2,
  k_message_render_done = 3,
};
enum {
  k_bbc_max_ssd_disc_size = (256 * 10 * 80),
  k_bbc_max_dsd_disc_size = (256 * 10 * 80 * 2),
};

struct bbc_struct;

struct bbc_struct* bbc_create(int mode,
                              int is_master,
                              uint8_t* p_os_rom,
                              int wd_1770_type,
                              int debug_flag,
                              int run_flag,
                              int print_flag,
                              int fast_flag,
                              int accurate_flag,
                              int fasttape_flag,
                              int test_map_flag,
                              const char* p_opt_flags,
                              const char* p_log_flags,
                              int32_t debug_stop_addr);
void bbc_destroy(struct bbc_struct* p_bbc);

void bbc_focus_lost_callback(void* p);

void bbc_power_on_reset(struct bbc_struct* p_bbc);
void bbc_enable_extended_rom_addressing(struct bbc_struct* p_bbc);
void bbc_load_rom(struct bbc_struct* p_bbc,
                  uint8_t index,
                  uint8_t* p_rom_src);
void bbc_save_rom(struct bbc_struct* p_bbc,
                  uint8_t index,
                  uint8_t* p_dest);
void bbc_make_sideways_ram(struct bbc_struct* p_bbc, uint8_t index);
uint8_t bbc_get_romsel(struct bbc_struct* p_bbc);
uint8_t bbc_get_acccon(struct bbc_struct* p_bbc);
void bbc_sideways_select(struct bbc_struct* p_bbc, uint8_t index);
void bbc_add_disc(struct bbc_struct* p_bbc,
                  const char* p_file_name,
                  int drive,
                  int is_writeable,
                  int is_mutable,
                  int convert_to_hfe);
void bbc_add_raw_disc(struct bbc_struct* p_bbc,
                      const char* p_file_name,
                      const char* p_spec);
void bbc_add_tape(struct bbc_struct* p_bbc, const char* p_file_name);
void bbc_set_stop_cycles(struct bbc_struct* p_bbc, uint64_t cycles);
void bbc_set_autoboot(struct bbc_struct* p_bbc, int autoboot_flag);
void bbc_set_commands(struct bbc_struct* p_bbc, const char* p_commands);

struct cpu_driver* bbc_get_cpu_driver(struct bbc_struct* p_bbc);
void bbc_get_registers(struct bbc_struct* p_bbc,
                       uint8_t* a,
                       uint8_t* x,
                       uint8_t* y,
                       uint8_t* s,
                       uint8_t* flags,
                       uint16_t* pc);
void bbc_set_registers(struct bbc_struct* p_bbc,
                       uint8_t a,
                       uint8_t x,
                       uint8_t y,
                       uint8_t s,
                       uint8_t flags,
                       uint16_t pc);
void bbc_set_pc(struct bbc_struct* p_bbc, uint16_t pc);

void bbc_run_async(struct bbc_struct* p_bbc);
uint32_t bbc_get_run_result(struct bbc_struct* p_bbc);
int bbc_check_do_break(struct bbc_struct* p_bbc);

struct state_6502* bbc_get_6502(struct bbc_struct* p_bbc);
struct via_struct* bbc_get_sysvia(struct bbc_struct* p_bbc);
struct via_struct* bbc_get_uservia(struct bbc_struct* p_bbc);
struct keyboard_struct* bbc_get_keyboard(struct bbc_struct* p_bbc);
struct sound_struct* bbc_get_sound(struct bbc_struct* p_bbc);
struct video_struct* bbc_get_video(struct bbc_struct* p_bbc);
struct render_struct* bbc_get_render(struct bbc_struct* p_bbc);
struct serial_struct* bbc_get_serial(struct bbc_struct* p_bbc);
struct cmos_struct* bbc_get_cmos(struct bbc_struct* p_bbc);
struct timing_struct* bbc_get_timing(struct bbc_struct* p_bbc);
struct wd_fdc_struct* bbc_get_wd_fdc(struct bbc_struct* p_bbc);
struct disc_drive_struct* bbc_get_drive_0(struct bbc_struct* p_bbc);
struct disc_drive_struct* bbc_get_drive_1(struct bbc_struct* p_bbc);

uint8_t bbc_get_IC32(struct bbc_struct* p_bbc);
void bbc_set_IC32(struct bbc_struct* p_bbc, uint8_t val);

uint8_t* bbc_get_mem_read(struct bbc_struct* p_bbc);
uint8_t* bbc_get_mem_write(struct bbc_struct* p_bbc);
void bbc_set_memory_block(struct bbc_struct* p_bbc,
                          uint16_t addr,
                          uint16_t len,
                          uint8_t* p_src_mem);
void bbc_memory_write(struct bbc_struct* p_bbc,
                      uint16_t addr_6502,
                      uint8_t val);
void bbc_get_address_details(struct bbc_struct* p_bbc,
                             int* p_out_is_register,
                             int* p_out_is_rom,
                             uint16_t addr_6502);

int bbc_get_run_flag(struct bbc_struct* p_bbc);
int bbc_get_print_flag(struct bbc_struct* p_bbc);
int bbc_get_vsync_wait_for_render(struct bbc_struct* p_bbc);

void bbc_set_channel_handles(struct bbc_struct* p_bbc,
                             intptr_t handle_channel_read_bbc,
                             intptr_t handle_channel_write_bbc,
                             intptr_t handle_channel_read_client,
                             intptr_t handle_channel_write_client);

struct bbc_message {
  uint64_t data[4];
};
void bbc_client_send_message(struct bbc_struct* p_bbc,
                             struct bbc_message* p_message);
void bbc_client_receive_message(struct bbc_struct* p_bbc,
                                struct bbc_message* p_out_message);

#endif /* BEEBJIT_BBC_H */
