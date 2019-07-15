#include "bbc.h"

#include "asm_x64_defs.h"
#include "bbc_options.h"
#include "cpu_driver.h"
#include "debug.h"
#include "defs_6502.h"
#include "intel_fdc.h"
#include "keyboard.h"
#include "memory_access.h"
#include "sound.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"
#include "via.h"
#include "video.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_bbc_os_rom_offset = 0xC000;
static const size_t k_bbc_sideways_offset = 0x8000;

static const size_t k_bbc_tick_rate = 2000000; /* 2Mhz. */
static const size_t k_system_wakeup_rate = 1000; /* 1ms / 1kHz. */

/* This data is from b-em, thanks b-em! */
static const int k_FE_1mhz_array[8] = { 1, 0, 1, 1, 0, 0, 1, 0 };

enum {
  k_addr_fred = 0xFC00,
  k_addr_jim = 0xFD00,
  k_addr_shiela = 0xFE00,
  k_addr_shiela_end = 0xFEFF,
  k_addr_crtc = 0xFE00,
  k_addr_acia = 0xFE08,
  k_addr_serial_ula = 0xFE10,
  k_addr_video_ula = 0xFE20,
  k_addr_rom_select = 0xFE30,
  k_addr_ram_select = 0xFE34,
  k_addr_sysvia = 0xFE40,
  k_addr_uservia = 0xFE60,
  k_addr_floppy = 0xFE80,
  k_addr_adc_status = 0xFEC0,
  k_addr_adc_high = 0xFEC1,
  k_addr_adc_low = 0xFEC2,
  k_addr_tube = 0xFEE0,
};

struct bbc_struct {
  /* Internal system mechanics. */
  pthread_t cpu_thread;
  int thread_allocated;
  int running;
  int message_cpu_fd;
  int message_client_fd;

  /* Settings. */
  uint8_t* p_os_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  int slow_flag;
  int vsync_wait_for_render;
  struct bbc_options options;
  int externally_clocked_via;
  int externally_clocked_crtc;

  /* Machine state. */
  struct state_6502* p_state_6502;
  struct memory_access memory_access;
  struct timing_struct* p_timing;
  int mem_fd;
  uint8_t* p_mem_raw;
  uint8_t* p_mem_read;
  uint8_t* p_mem_write;
  uint8_t* p_mem_read_ind;
  uint8_t* p_mem_write_ind;
  uint8_t* p_mem_sideways;
  struct via_struct* p_system_via;
  struct via_struct* p_user_via;
  struct keyboard_struct* p_keyboard;
  struct sound_struct* p_sound;
  struct video_struct* p_video;
  struct intel_fdc_struct* p_intel_fdc;
  struct cpu_driver* p_cpu_driver;
  struct debug_struct* p_debug;
  uint32_t exit_value;
  /* Timing support. */
  size_t timer_id;
  uint64_t cycles_per_run_fast;
  uint64_t cycles_per_run_normal;
  uint64_t last_time_us;
  uint8_t romsel;
  int is_sideways_ram_bank[k_bbc_num_roms];
};

static int
bbc_is_always_ram_address(void* p, uint16_t addr) {
  (void) p;

  return (addr < k_bbc_ram_size);
}

static uint16_t
bbc_read_needs_callback_above(void* p) {
  (void) p;

  /* Selects 0xFC00 - 0xFFFF which is broader than the needed 0xFC00 - 0xFEFF,
   * but that's fine.
   */
  return 0xFC00;
}

static uint16_t
bbc_write_needs_callback_above(void* p) {
  (void) p;

  /* Selects 0xFC00 - 0xFFFF. */
  /* Doesn't select the whole ROM region 0x8000 - 0xFC00 because ROM write
   * squashing is handled by writing to to the "write" mapping, which has the
   * ROM regions backed by dummy RAM.
   */
  return 0xFC00;
}

static int
bbc_read_needs_callback(void* p, uint16_t addr) {
  (void) p;

  if (addr >= 0xFC00 && addr < 0xFF00) {
    return 1;
  }

  return 0;
}

static int
bbc_write_needs_callback(void* p, uint16_t addr) {
  (void) p;

  /* Same range as for reads. */
  return bbc_read_needs_callback(p, addr);
}

int
bbc_is_special_read_address(struct bbc_struct* p_bbc,
                            uint16_t addr_low,
                            uint16_t addr_high) {
  (void) p_bbc;

  if (addr_low >= k_bbc_registers_start &&
      addr_low < k_bbc_registers_start + k_bbc_registers_len) {
    return 1;
  }
  if (addr_high >= k_bbc_registers_start &&
      addr_high < k_bbc_registers_start + k_bbc_registers_len) {
    return 1;
  }
  if (addr_low < k_bbc_registers_start &&
      addr_high >= k_bbc_registers_start + k_bbc_registers_len) {
    return 1;
  }
  return 0;
}

int
bbc_is_special_write_address(struct bbc_struct* p_bbc,
                             uint16_t addr_low,
                             uint16_t addr_high) {
  (void) p_bbc;

  if (addr_low >= k_bbc_sideways_offset) {
    return 1;
  }
  if (addr_high >= k_bbc_sideways_offset) {
    return 1;
  }
  return 0;
}

static int
bbc_is_1mhz_address(uint16_t addr) {
  if ((addr < k_addr_fred) || (addr > k_addr_shiela_end)) {
    return 0;
  }
  if (addr < k_addr_shiela) {
    return 1;
  }

  return k_FE_1mhz_array[((addr >> 5) & 7)];
}

uint8_t
bbc_read_callback(void* p, uint16_t addr) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct state_6502* p_state_6502;

  /* We have an imprecise match for abx and aby addressing modes so we may get
   * here with a non-registers address, or also for the 0xff00 - 0xffff range.
   */
  if (addr < k_bbc_registers_start ||
      addr >= k_bbc_registers_start + k_bbc_registers_len) {
    uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);
    return p_mem_read[addr];
  }

  p_state_6502 = bbc_get_6502(p_bbc);

  if (bbc_is_1mhz_address(addr) && (state_6502_get_cycles(p_state_6502) & 1)) {
    /* If a 1Mhz peripheral is accessed on a odd cycle, wait a cycle to catch
     * the 1Mhz train.
     */
    struct timing_struct* p_timing = p_bbc->p_timing;
    int64_t countdown = timing_get_countdown(p_timing);
    countdown--;
    (void) timing_advance_time(p_timing, countdown);
  }

  if (addr >= k_addr_sysvia && addr <= k_addr_sysvia + 0x1f) {
    return via_read(p_bbc->p_system_via, (addr & 0xf));
  }
  if (addr >= k_addr_uservia && addr <= k_addr_uservia + 0x1f) {
    return via_read(p_bbc->p_user_via, (addr & 0xf));
  }

  switch (addr) {
  case k_addr_acia:
    /* No ACIA interrupt (bit 7). */
    return 0;
  case 0xFE18:
    /* Only used in Master model but read by Synchron. */
    break;
  case k_addr_ram_select:
    /* Ignore RAM select. Fall through.
     * (On a B+ / Master, it does shadow RAM trickery.)
     */
    break;
  case k_addr_floppy:
  case (k_addr_floppy + 1):
  case (k_addr_floppy + 4):
    return intel_fdc_read(p_bbc->p_intel_fdc, addr);
  case k_addr_adc_status:
    /* No ADC attention needed (bit 6). */
    return 0;
  case k_addr_adc_high:
  case k_addr_adc_low:
    /* No ADC values. */
    return 0;
  case k_addr_tube:
    /* Not present -- fall through to return 0xfe. */
    break;
  default:
    if (addr >= k_addr_shiela) {
      printf("unknown read: %x\n", addr);
      assert(0);
    }
  }
  /* EMU NOTE: These return values copied from b-em / jsbeeb. */
  /* EMU TODO: confirm on a real BBC. */
  if (addr < k_addr_shiela) {
    return 0xFF;
  }
  return 0xFE;
}

uint8_t
bbc_get_romsel(struct bbc_struct* p_bbc) {
  return p_bbc->romsel;
}

void
bbc_sideways_select(struct bbc_struct* p_bbc, uint8_t index) {
  /* The broad approach here is: slower sideways bank switching in order to
   * enable faster memory accesses at runtime.
   * Other emulators (jsbeeb, b-em) appear to make a different tradeoff: faster
   * bank switching but slower memory accesses.
   * By just copying ROM bytes into the single memory chunk representing the
   * 6502 address space, memory access at runtime can be direct, instead of
   * having to go through lookup arrays.
   */
  int curr_is_ram;
  int new_is_ram;

  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  uint8_t* p_sideways_src = p_bbc->p_mem_sideways;
  uint8_t* p_mem_sideways = (p_bbc->p_mem_raw + k_bbc_sideways_offset);
  uint8_t curr_bank = p_bbc->romsel;

  assert(curr_bank < k_bbc_num_roms);
  /* NOTE: this assert isn't really valid as setting the romsel to e.g. 0xFF
   * likely just masks with 0x0F in hardware.
   * Still, crazy romsel values would be a novelty so we'll leave the assert
   * in to see if it fires. Safety for optimized builds is provided by the mask
   * directly below.
   */
  assert(index < k_bbc_num_roms);

  index &= 0x0F;

  curr_is_ram = (p_bbc->is_sideways_ram_bank[curr_bank] != 0);
  new_is_ram = (p_bbc->is_sideways_ram_bank[index] != 0);

  p_sideways_src += (index * k_bbc_rom_size);

  /* If current bank is RAM, save it. */
  if (p_bbc->is_sideways_ram_bank[curr_bank]) {
    uint8_t* p_sideways_dest = p_bbc->p_mem_sideways;
    p_sideways_dest += (curr_bank * k_bbc_rom_size);
    (void) memcpy(p_sideways_dest, p_mem_sideways, k_bbc_rom_size);
  }

  (void) memcpy(p_mem_sideways, p_sideways_src, k_bbc_rom_size);

  p_cpu_driver->p_funcs->memory_range_invalidate(p_cpu_driver,
                                                 k_bbc_sideways_offset,
                                                 k_bbc_rom_size);

  /* If we flipped from ROM to RAM or visa versa, we need to update the write
   * mapping with either a dummy area (ROM) or the real sideways area (RAM).
   */
  if (curr_is_ram ^ new_is_ram) {
    if (new_is_ram) {
      (void) util_get_fixed_mapping_from_fd(
          p_bbc->mem_fd,
          p_bbc->p_mem_write,
          (k_bbc_sideways_offset + k_bbc_rom_size));
      (void) util_get_fixed_mapping_from_fd(
          p_bbc->mem_fd,
          p_bbc->p_mem_write_ind,
          (k_bbc_sideways_offset + k_bbc_rom_size));
    } else {
      (void) util_get_fixed_anonymous_mapping(
          (p_bbc->p_mem_write + k_bbc_sideways_offset),
          k_bbc_rom_size);
      (void) util_get_fixed_anonymous_mapping(
          (p_bbc->p_mem_write_ind + k_bbc_sideways_offset),
          k_bbc_rom_size);
    }
  }

  p_bbc->romsel = index;
}

void
bbc_write_callback(void* p, uint16_t addr, uint8_t val) {
  struct state_6502* p_state_6502;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct video_struct* p_video = bbc_get_video(p_bbc);

  /* We bounce here for ROM writes as well as register writes; ROM writes
   * are simply squashed.
   */
  if (addr < k_bbc_registers_start ||
      addr >= k_bbc_registers_start + k_bbc_registers_len) {
    uint8_t* p_mem_write = bbc_get_mem_write(p_bbc);
    if (addr >= k_bbc_os_rom_offset) {
      return;
    }
    /* Possible to get here for a write at the end of a RAM region, e.g.
     * STA $7F01,X
     */
    p_mem_write[addr] = val;
    return;
  }

  p_state_6502 = bbc_get_6502(p_bbc);

  if (bbc_is_1mhz_address(addr) && (state_6502_get_cycles(p_state_6502) & 1)) {
    /* If a 1Mhz peripheral is accessed on a odd cycle, wait a cycle to catch
     * the 1Mhz train.
     */
    struct timing_struct* p_timing = p_bbc->p_timing;
    int64_t countdown = timing_get_countdown(p_timing);
    countdown--;
    (void) timing_advance_time(p_timing, countdown);
  }

  if (addr >= k_addr_sysvia && addr <= k_addr_sysvia + 0x1f) {
    via_write(p_bbc->p_system_via, (addr & 0xf), val);
    return;
  }
  if (addr >= k_addr_uservia && addr <= k_addr_uservia + 0x1f) {
    via_write(p_bbc->p_user_via, (addr & 0xf), val);
    return;
  }

  switch (addr) {
  case (k_addr_crtc + 0):
  case (k_addr_crtc + 1):
    video_crtc_write(p_video, (addr & 0xf), val);
    break;
  case k_addr_acia:
    printf("ignoring ACIA write\n");
    break;
  case k_addr_serial_ula:
    printf("ignoring serial ULA write\n");
    break;
  case (k_addr_video_ula + 0):
  case (k_addr_video_ula + 1):
    video_ula_write(p_video, (addr & 0xf), val);
    break;
  case k_addr_rom_select:
  case (k_addr_rom_select + 1):
  case (k_addr_rom_select + 2):
  case (k_addr_rom_select + 3):
    if (val != p_bbc->romsel) {
      bbc_sideways_select(p_bbc, val);
    }
    break;
  case k_addr_ram_select:
    /* Ignore RAM select. Doesn't do anything on a Model B.
     * (On a B+ / Master, it does shadow RAM trickery.)
     */
    break;
  case k_addr_floppy:
  case (k_addr_floppy + 1):
    intel_fdc_write(p_bbc->p_intel_fdc, addr, val);
    break;
  case k_addr_adc_status:
    printf("ignoring ADC status write\n");
    break;
  case k_addr_tube:
    printf("ignoring tube write\n");
    break;
  default:
    printf("unknown write: %x, %x\n", addr, val);
    assert(0);
  }
}

void
bbc_client_send_message(struct bbc_struct* p_bbc, char message) {
  int ret = write(p_bbc->message_client_fd, &message, 1);
  if (ret != 1) {
    errx(1, "write failed");
  }
}

static void
bbc_cpu_send_message(struct bbc_struct* p_bbc, char message) {
  int ret = write(p_bbc->message_cpu_fd, &message, 1);
  if (ret != 1) {
    errx(1, "write failed");
  }
}

char
bbc_client_receive_message(struct bbc_struct* p_bbc) {
  char message;

  int ret = read(p_bbc->message_client_fd, &message, 1);
  if (ret != 1) {
    errx(1, "read failed");
  }

  return message;
}

static char
bbc_cpu_receive_message(struct bbc_struct* p_bbc) {
  char message;

  int ret = read(p_bbc->message_cpu_fd, &message, 1);
  if (ret != 1) {
    errx(1, "read failed");
  }

  return message;
}

static void
bbc_framebuffer_ready_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  bbc_cpu_send_message(p_bbc, k_message_vsync);
  if (bbc_get_vsync_wait_for_render(p_bbc)) {
    uint8_t message = bbc_cpu_receive_message(p_bbc);
    (void) message;
    assert(message == k_message_render_done);
  }
}

struct bbc_struct*
bbc_create(int mode,
           uint8_t* p_os_rom,
           int debug_flag,
           int run_flag,
           int print_flag,
           int slow_flag,
           int accurate_flag,
           const char* p_opt_flags,
           const char* p_log_flags,
           uint16_t debug_stop_addr) {
  struct timing_struct* p_timing;
  struct state_6502* p_state_6502;
  struct debug_struct* p_debug;
  int pipefd[2];
  int ret;

  int externally_clocked_via = 1;
  int externally_clocked_crtc = 1;

  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }
  (void) memset(p_bbc, '\0', sizeof(struct bbc_struct));

  ret = pipe(&pipefd[0]);
  if (ret != 0) {
    errx(1, "pipe failed");
  }

  util_get_channel_fds(&p_bbc->message_cpu_fd, &p_bbc->message_client_fd);

  p_bbc->thread_allocated = 0;
  p_bbc->running = 0;
  p_bbc->p_os_rom = p_os_rom;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;
  p_bbc->slow_flag = slow_flag;
  p_bbc->vsync_wait_for_render = 1;
  p_bbc->exit_value = 0;

  p_bbc->last_time_us = 0;

  if (util_has_option(p_opt_flags, "video:no-vsync-wait-for-render")) {
    p_bbc->vsync_wait_for_render = 0;
  }

  p_bbc->mem_fd = util_get_memory_fd(k_6502_addr_space_size);
  if (p_bbc->mem_fd < 0) {
    errx(1, "util_get_memory_fd failed");
  }

  p_bbc->p_mem_raw =
      util_get_guarded_mapping_from_fd(
          p_bbc->mem_fd,
          (void*) (size_t) K_BBC_MEM_RAW_ADDR,
          k_6502_addr_space_size);

  /* Runtime memory regions.
   * The write regions differ from the read regions for 6502 ROM addresses.
   * Writes to those in the writable region write to a dummy backing store to
   * avoid a fault but also to avoid modifying 6502 ROM.
   * The indirect regions are the same as the normal read / write regions
   * except the page containing the hardware registers is marked inaccessible.
   * This is used to enable a fast common case (no checks for hardware register
   * access for indirect reads and writes) but work for the exceptional case
   * via a fault + fixup.
   */
  p_bbc->p_mem_read_ind =
      util_get_guarded_mapping_from_fd(
          p_bbc->mem_fd,
          (void*) (size_t) K_BBC_MEM_READ_IND_ADDR,
          k_6502_addr_space_size);
  p_bbc->p_mem_write_ind =
      util_get_guarded_mapping_from_fd(
          p_bbc->mem_fd,
          (void*) (size_t) K_BBC_MEM_WRITE_IND_ADDR,
          k_6502_addr_space_size);

  p_bbc->p_mem_read =
      util_get_guarded_mapping_from_fd(
          p_bbc->mem_fd,
          (void*) (size_t) K_BBC_MEM_READ_FULL_ADDR,
          k_6502_addr_space_size);
  p_bbc->p_mem_write =
      util_get_guarded_mapping_from_fd(
          p_bbc->mem_fd,
          (void*) (size_t) K_BBC_MEM_WRITE_FULL_ADDR,
          k_6502_addr_space_size);

  p_bbc->p_mem_sideways = malloc(k_bbc_rom_size * k_bbc_num_roms);
  (void) memset(p_bbc->p_mem_sideways, '\0', (k_bbc_rom_size * k_bbc_num_roms));

  /* Install the dummy writeable ROM regions. */
  (void) util_get_fixed_anonymous_mapping(
      (p_bbc->p_mem_write + k_bbc_ram_size),
      (k_6502_addr_space_size - k_bbc_ram_size));
  (void) util_get_fixed_anonymous_mapping(
      (p_bbc->p_mem_write_ind + k_bbc_ram_size),
      (k_6502_addr_space_size - k_bbc_ram_size));

  /* Make the ROM readonly in the read mappings used at runtime. */
  util_make_mapping_read_only((p_bbc->p_mem_read + k_bbc_ram_size),
                              (k_6502_addr_space_size - k_bbc_ram_size));
  util_make_mapping_read_only((p_bbc->p_mem_read_ind + k_bbc_ram_size),
                              (k_6502_addr_space_size - k_bbc_ram_size));

  /* Make the registers page inaccessible in the indirect read / write
   * mappings. This enables an optimization: indirect reads and writes can
   * avoid expensive checks for hitting registers, which is rare, and rely
   * instead on a fault + fixup.
   */
  util_make_mapping_none(
      (p_bbc->p_mem_read_ind + K_BBC_MEM_INACCESSIBLE_OFFSET),
      K_BBC_MEM_INACCESSIBLE_LEN);
  util_make_mapping_none(
      (p_bbc->p_mem_write_ind + K_BBC_MEM_INACCESSIBLE_OFFSET),
      K_BBC_MEM_INACCESSIBLE_LEN);

  p_bbc->memory_access.p_mem_read = p_bbc->p_mem_read;
  p_bbc->memory_access.p_mem_write = p_bbc->p_mem_write;
  p_bbc->memory_access.p_callback_obj = p_bbc;
  p_bbc->memory_access.memory_is_always_ram = bbc_is_always_ram_address;
  p_bbc->memory_access.memory_read_needs_callback_above =
      bbc_read_needs_callback_above;
  p_bbc->memory_access.memory_write_needs_callback_above =
      bbc_write_needs_callback_above;
  p_bbc->memory_access.memory_read_needs_callback = bbc_read_needs_callback;
  p_bbc->memory_access.memory_write_needs_callback = bbc_write_needs_callback;
  p_bbc->memory_access.memory_read_callback = bbc_read_callback;
  p_bbc->memory_access.memory_write_callback = bbc_write_callback;

  p_bbc->options.debug_subsystem_active = debug_subsystem_active;
  p_bbc->options.debug_active_at_addr = debug_active_at_addr;
  p_bbc->options.debug_callback = debug_callback;
  p_bbc->options.p_opt_flags = p_opt_flags;
  p_bbc->options.p_log_flags = p_log_flags;
  p_bbc->options.accurate = accurate_flag;

  if (accurate_flag) {
    externally_clocked_via = 0;
    externally_clocked_crtc = 0;
  }
  p_bbc->externally_clocked_via = externally_clocked_via;
  p_bbc->externally_clocked_crtc = externally_clocked_crtc;

  p_timing = timing_create(k_bbc_tick_rate);
  if (p_timing == NULL) {
    errx(1, "timing_create failed");
  }
  p_bbc->p_timing = p_timing;

  p_state_6502 = state_6502_create(p_timing);
  if (p_state_6502 == NULL) {
    errx(1, "state_6502_create failed");
  }
  p_bbc->p_state_6502 = p_state_6502;

  p_bbc->p_system_via = via_create(k_via_system,
                                   externally_clocked_via,
                                   p_timing,
                                   p_bbc);
  if (p_bbc->p_system_via == NULL) {
    errx(1, "via_create failed");
  }
  p_bbc->p_user_via = via_create(k_via_user,
                                 externally_clocked_via,
                                 p_timing,
                                 p_bbc);
  if (p_bbc->p_system_via == NULL) {
    errx(1, "via_create failed");
  }

  p_bbc->p_keyboard = keyboard_create();
  if (p_bbc->p_keyboard == NULL) {
    errx(1, "keyboard_create failed");
  }

  p_bbc->p_sound = sound_create();
  if (p_bbc->p_sound == NULL) {
    errx(1, "sound_create failed");
  }

  p_bbc->p_video = video_create(p_bbc->p_mem_read,
                                externally_clocked_crtc,
                                p_timing,
                                p_bbc->p_system_via,
                                bbc_framebuffer_ready_callback,
                                p_bbc,
                                &p_bbc->options);
  if (p_bbc->p_video == NULL) {
    errx(1, "video_create failed");
  }

  p_bbc->p_intel_fdc = intel_fdc_create(p_state_6502, p_timing);
  if (p_bbc->p_intel_fdc == NULL) {
    errx(1, "intel_fdc_create failed");
  }

  p_debug = debug_create(p_bbc, debug_flag, debug_stop_addr);
  if (p_debug == NULL) {
    errx(1, "debug_create failed");
  }

  p_bbc->options.p_debug_object = p_debug;

  p_bbc->p_cpu_driver = cpu_driver_alloc(mode,
                                         p_state_6502,
                                         &p_bbc->memory_access,
                                         p_timing,
                                         &p_bbc->options);
  if (p_bbc->p_cpu_driver == NULL) {
    errx(1, "cpu_driver_alloc failed");
  }

  bbc_full_reset(p_bbc);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  int ret;

  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  volatile int* p_running = &p_bbc->running;
  volatile int* p_thread_allocated = &p_bbc->thread_allocated;

  (void) p_running;
  assert(!*p_running);

  if (*p_thread_allocated) {
    int ret = pthread_join(p_bbc->cpu_thread, NULL);
    if (ret != 0) {
      errx(1, "pthread_join failed");
    }
  }

  p_cpu_driver->p_funcs->destroy(p_cpu_driver);

  debug_destroy(p_bbc->p_debug);
  video_destroy(p_bbc->p_video);
  sound_destroy(p_bbc->p_sound);
  keyboard_destroy(p_bbc->p_keyboard);
  via_destroy(p_bbc->p_system_via);
  via_destroy(p_bbc->p_user_via);
  state_6502_destroy(p_bbc->p_state_6502);
  timing_destroy(p_bbc->p_timing);
  util_free_guarded_mapping(p_bbc->p_mem_raw, k_6502_addr_space_size);
  util_free_guarded_mapping(p_bbc->p_mem_read, k_6502_addr_space_size);
  util_free_guarded_mapping(p_bbc->p_mem_write, k_6502_addr_space_size);
  util_free_guarded_mapping(p_bbc->p_mem_read_ind, k_6502_addr_space_size);
  util_free_guarded_mapping(p_bbc->p_mem_write_ind, k_6502_addr_space_size);

  ret = close(p_bbc->mem_fd);
  if (ret != 0) {
    errx(1, "close failed");
  }

  free(p_bbc);
}

void
bbc_load_rom(struct bbc_struct* p_bbc,
             uint8_t index,
             uint8_t* p_rom_src) {
  uint8_t* p_rom_dest = p_bbc->p_mem_sideways;

  assert(index < k_bbc_num_roms);

  p_rom_dest += (index * k_bbc_rom_size);
  (void) memcpy(p_rom_dest, p_rom_src, k_bbc_rom_size);
}

void
bbc_save_rom(struct bbc_struct* p_bbc,
             uint8_t index,
             uint8_t* p_dest) {
  uint8_t* p_rom_src = p_bbc->p_mem_sideways;

  assert(index < k_bbc_num_roms);

  p_rom_src += (index * k_bbc_rom_size);
  (void) memcpy(p_dest, p_rom_src, k_bbc_rom_size);
}

void
bbc_make_sideways_ram(struct bbc_struct* p_bbc, uint8_t index) {
  assert(index < k_bbc_num_roms);
  p_bbc->is_sideways_ram_bank[index] = 1;
}

void
bbc_full_reset(struct bbc_struct* p_bbc) {
  uint16_t init_pc;

  uint8_t* p_mem_raw = p_bbc->p_mem_raw;
  uint8_t* p_os_start = (p_mem_raw + k_bbc_os_rom_offset);
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);

  /* Clear memory / ROMs. */
  (void) memset(p_mem_raw, '\0', k_6502_addr_space_size);

  /* Copy in OS ROM. */
  (void) memcpy(p_os_start, p_bbc->p_os_rom, k_bbc_rom_size);

  bbc_sideways_select(p_bbc, 0);

  state_6502_reset(p_state_6502);

  /* Initial 6502 state. */
  init_pc = (p_mem_raw[k_6502_vector_reset] |
             (p_mem_raw[k_6502_vector_reset + 1] << 8));
  state_6502_set_pc(p_state_6502, init_pc);
}

struct cpu_driver*
bbc_get_cpu_driver(struct bbc_struct* p_bbc) {
  return p_bbc->p_cpu_driver;
}

void
bbc_get_registers(struct bbc_struct* p_bbc,
                  uint8_t* a,
                  uint8_t* x,
                  uint8_t* y,
                  uint8_t* s,
                  uint8_t* flags,
                  uint16_t* pc) {
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  state_6502_get_registers(p_state_6502, a, x, y, s, flags, pc);
}

size_t
bbc_get_cycles(struct bbc_struct* p_bbc) {
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  return state_6502_get_cycles(p_state_6502);
}

void
bbc_set_registers(struct bbc_struct* p_bbc,
                  uint8_t a,
                  uint8_t x,
                  uint8_t y,
                  uint8_t s,
                  uint8_t flags,
                  uint16_t pc) {
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);
}

void
bbc_set_pc(struct bbc_struct* p_bbc, uint16_t pc) {
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  state_6502_set_pc(p_state_6502, pc);
}

struct state_6502*
bbc_get_6502(struct bbc_struct* p_bbc) {
  return p_bbc->p_state_6502;
}

struct via_struct*
bbc_get_sysvia(struct bbc_struct* p_bbc) {
  return p_bbc->p_system_via;
}

struct via_struct*
bbc_get_uservia(struct bbc_struct* p_bbc) {
  return p_bbc->p_user_via;
}

struct keyboard_struct*
bbc_get_keyboard(struct bbc_struct* p_bbc) {
  return p_bbc->p_keyboard;
}

struct sound_struct*
bbc_get_sound(struct bbc_struct* p_bbc) {
  return p_bbc->p_sound;
}

struct video_struct*
bbc_get_video(struct bbc_struct* p_bbc) {
  return p_bbc->p_video;
}

uint8_t*
bbc_get_mem_read(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem_read;
}

uint8_t*
bbc_get_mem_write(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem_write;
}

void
bbc_set_memory_block(struct bbc_struct* p_bbc,
                     uint16_t addr,
                     uint16_t len,
                     uint8_t* p_src_mem) {
  size_t count = 0;
  while (count < len) {
    bbc_memory_write(p_bbc, addr, p_src_mem[count]);
    count++;
    addr++;
  }
}

void
bbc_memory_write(struct bbc_struct* p_bbc,
                 uint16_t addr_6502,
                 uint8_t val) {
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  uint8_t* p_mem_raw = p_bbc->p_mem_raw;

  /* Allow a forced write to ROM using this API -- use the fully read/write
   * mapping.
   */
  p_mem_raw[addr_6502] = val;

  p_cpu_driver->p_funcs->memory_range_invalidate(p_cpu_driver, addr_6502, 1);
}

int
bbc_get_run_flag(struct bbc_struct* p_bbc) {
  return p_bbc->run_flag;
}

int
bbc_get_print_flag(struct bbc_struct* p_bbc) {
  return p_bbc->print_flag;
}

int
bbc_get_vsync_wait_for_render(struct bbc_struct* p_bbc) {
  return p_bbc->vsync_wait_for_render;
}

static void
bbc_cycles_timer_callback(void* p) {
  uint64_t delta_us;
  uint64_t cycles_next_run;
  int64_t refreshed_time;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct keyboard_struct* p_keyboard = bbc_get_keyboard(p_bbc);
  struct timing_struct* p_timing = p_bbc->p_timing;
  uint64_t curr_time_us = util_gettime_us();

  /* Check for special alt key combos to change emulator behavior. */
  if (keyboard_check_and_clear_alt_key(p_keyboard, 'F')) {
    p_bbc->slow_flag = !p_bbc->slow_flag;
  }

  if (p_bbc->slow_flag) {
    /* Slow mode.
     * Slow mode, or "real time" mode is where the system executes at normal
     * speed, i.e. a 2Mhz BBC executes at 2Mhz in real time. Host CPU usage
     * may be small.
     * This is achieved by executing the system for some number of cycles, then
     * sleeping however long is required to match to real time, then repeating.
     * If the system is executed for a smaller number of cycles per sleep,
     * specifically some fraction of a 50Hz frame, a highly responsive system
     * results.
     */
    uint64_t next_wakeup_time_us;
    int64_t spare_time_us;

    cycles_next_run = p_bbc->cycles_per_run_normal;
    delta_us = (1000000 / k_system_wakeup_rate);
    next_wakeup_time_us = (p_bbc->last_time_us + delta_us);
    spare_time_us = (next_wakeup_time_us - curr_time_us);
    if (spare_time_us >= 0) {
      util_sleep_us(spare_time_us);
      curr_time_us = next_wakeup_time_us;
    } else {
      /* Missed a tick.
       * In all cases, don't sleep.
       */
       if (spare_time_us >= -20000) {
         /* If it's a small miss, keep the existing timing expectations so that
          * virtual time can catch up to wall time.
          */
        curr_time_us = next_wakeup_time_us;
       } else {
         /* If it's a large miss, perhaps the system was paused in the
          * debugger or some other good reason, so reset timing expectations
          * to avoid a huge flurry of catch-up at 100% CPU.
          */
       }
    }
  } else {
    /* Fast mode.
     * Fast mode is where the system executes as fast as the host CPU can
     * manage. Host CPU usage for the system's main thread will be 100%.
     * Effective system CPU rates of many GHz are likely to be obtained.
     */
    cycles_next_run = p_bbc->cycles_per_run_fast;
    /* TODO: limit delta_us max size in case system was paused? */
    delta_us = (curr_time_us - p_bbc->last_time_us);
  }

  p_bbc->last_time_us = curr_time_us;

  (void) timing_adjust_timer_value(p_timing,
                                   &refreshed_time,
                                   p_bbc->timer_id,
                                   cycles_next_run);

  assert(refreshed_time > 0);

  /* Provide the wall time delta to various modules.
   * In inaccurate modes, this wall time may be used to advance state.
   */
  via_apply_wall_time_delta(p_bbc->p_system_via, delta_us);
  via_apply_wall_time_delta(p_bbc->p_user_via, delta_us);
  video_apply_wall_time_delta(p_bbc->p_video, delta_us);

  /* Read sysvia port A to update keyboard state and fire interrupts. */
  (void) via_read_port_a(p_bbc->p_system_via);
}

static void
bbc_start_timer_tick(struct bbc_struct* p_bbc) {
  uint32_t option_cycles_per_run;
  uint64_t speed;
  struct timing_struct* p_timing = p_bbc->p_timing;
  p_bbc->timer_id = timing_register_timer(p_timing,
                                          bbc_cycles_timer_callback,
                                          p_bbc);

  /* Normal mode is when the system is running at real time, aka. "slow" mode.
   * Fast mode is when the system is running the CPU as fast as possible.
   * In fast mode, peripherals may run either in real time, or synced with
   * fast CPU.
   */
  p_bbc->cycles_per_run_normal = (k_bbc_tick_rate / k_system_wakeup_rate);

  /* For fast mode, estimate how fast the CPU really is (based on JIT mode,
   * debug mode, etc.) And then arrange to check in at approximately every
   * k_system_wakeup_rate. This will ensure reasonable timer resolution and
   * excellent keyboard response.
   */
  if (p_bbc->debug_flag) {
    /* Assume 20Mhz speed or so. */
    speed = (20ull * 1000 * 1000);
  } else {
    /* 500Mhz for now. Something like JIT is much much faster but I think
     * there's a problem with tight hardware register poll loops (e.g.
     * vsync wait) being much slower.
     */
    speed = (500ull * 1000 * 1000);
  }

  p_bbc->cycles_per_run_fast = (speed / k_system_wakeup_rate);

  if (util_get_u32_option(&option_cycles_per_run,
                          p_bbc->options.p_opt_flags,
                          "bbc:cycles-per-run=")) {
    p_bbc->cycles_per_run_fast = option_cycles_per_run;
  }

  (void) timing_start_timer_with_value(p_timing, p_bbc->timer_id, 1);

  p_bbc->last_time_us = util_gettime_us();
}

static void*
bbc_cpu_thread(void* p) {
  int exited;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  bbc_start_timer_tick(p_bbc);

  exited = p_cpu_driver->p_funcs->enter(p_cpu_driver);
  (void) exited;
  assert(exited == 1);
  assert(p_cpu_driver->p_funcs->has_exited(p_cpu_driver) == 1);

  p_bbc->running = 0;
  p_bbc->exit_value = p_cpu_driver->p_funcs->get_exit_value(p_cpu_driver);

  bbc_cpu_send_message(p_bbc, k_message_exited);

  return NULL;
}

void
bbc_run_async(struct bbc_struct* p_bbc) {
  int ret = pthread_create(&p_bbc->cpu_thread, NULL, bbc_cpu_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create jit thread");
  }

  assert(!p_bbc->thread_allocated);
  assert(!p_bbc->running);

  p_bbc->thread_allocated = 1;
  p_bbc->running = 1;

  sound_start_playing(p_bbc->p_sound);
}

uint32_t
bbc_get_run_result(struct bbc_struct* p_bbc) {
  volatile uint32_t* p_ret = &p_bbc->exit_value;
  volatile int* p_running = &p_bbc->running;

  (void) p_running;
  assert(!*p_running);

  return *p_ret;
}

size_t
bbc_get_client_handle(struct bbc_struct* p_bbc) {
  return p_bbc->message_client_fd;
}

void
bbc_load_disc(struct bbc_struct* p_bbc, uint8_t* p_data, size_t length) {
  intel_fdc_load_ssd(p_bbc->p_intel_fdc, 0, p_data, length);
}

static void
bbc_stop_cycles_timer_callback(void* p) {
  (void) p;
  __builtin_trap();
}

void
bbc_set_stop_cycles(struct bbc_struct* p_bbc, uint64_t cycles) {
  struct timing_struct* p_timing = p_bbc->p_timing;
  size_t id = timing_register_timer(p_bbc->p_timing,
                                    bbc_stop_cycles_timer_callback,
                                    NULL);
  (void) timing_start_timer_with_value(p_timing, id, cycles);
}
