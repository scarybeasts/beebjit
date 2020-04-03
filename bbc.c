#include "bbc.h"

#include "asm_x64_defs.h"
#include "bbc_options.h"
#include "cpu_driver.h"
#include "debug.h"
#include "defs_6502.h"
#include "disc.h"
#include "disc_drive.h"
#include "intel_fdc.h"
#include "keyboard.h"
#include "log.h"
#include "memory_access.h"
#include "os_alloc.h"
#include "os_channel.h"
#include "os_thread.h"
#include "os_time.h"
#include "render.h"
#include "serial.h"
#include "sound.h"
#include "state_6502.h"
#include "tape.h"
#include "teletext.h"
#include "timing.h"
#include "util.h"
#include "via.h"
#include "video.h"

#include <assert.h>
#include <string.h>

static const size_t k_bbc_os_rom_offset = 0xC000;
static const size_t k_bbc_sideways_offset = 0x8000;

static const size_t k_bbc_tick_rate = 2000000; /* 2Mhz. */
static const size_t k_bbc_default_wakeup_rate = 1000; /* 1ms / 1kHz. */

/* This data is from b-em, thanks b-em! */
static const int k_FE_1mhz_array[8] = { 1, 0, 1, 1, 0, 0, 1, 0 };

/* EMU: where given, any ranges are confirmed on a real model B. */
enum {
  k_addr_fred = 0xFC00,
  k_addr_jim = 0xFD00,
  k_addr_shiela = 0xFE00,
  k_addr_shiela_end = 0xFEFF,
  /* &FE00 - &FE07. */
  k_addr_crtc = 0xFE00,
  /* &FE08 - &FE0F. */
  k_addr_acia = 0xFE08,
  /* &FE10 - &FE17. */
  k_addr_serial_ula = 0xFE10,
  /* &FE18 - &FE1B. */
  k_addr_master_adc = 0xFE18,
  /* &FE20 - &FE2F. */
  k_addr_video_ula = 0xFE20,
  /* &FE30 - &FE3F. */
  k_addr_rom_select = 0xFE30,
  /* &FE40 - &FE5F. */
  k_addr_sysvia = 0xFE40,
  /* &FE60 - &FE6F. */
  k_addr_uservia = 0xFE60,
  /* &FE80 - &FE9F. */
  k_addr_floppy = 0xFE80,
  k_addr_econet = 0xFEA0,
  k_addr_adc = 0xFEC0,
  k_addr_tube = 0xFEE0,
};

struct bbc_struct {
  /* Internal system mechanics. */
  struct os_thread_struct* p_thread_cpu;
  int thread_allocated;
  int running;
  intptr_t handle_channel_read_bbc;
  intptr_t handle_channel_write_bbc;
  intptr_t handle_channel_read_client;
  intptr_t handle_channel_write_client;
  uint32_t exit_value;
  intptr_t mem_handle;
  int is_64k_mappings;

  /* Settings. */
  uint8_t* p_os_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  int fast_flag;
  int test_map_flag;
  int vsync_wait_for_render;
  struct bbc_options options;

  /* Machine state. */
  struct state_6502* p_state_6502;
  struct memory_access memory_access;
  struct timing_struct* p_timing;
  struct os_alloc_mapping* p_mapping_raw;
  struct os_alloc_mapping* p_mapping_read;
  struct os_alloc_mapping* p_mapping_write;
  struct os_alloc_mapping* p_mapping_write_2;
  struct os_alloc_mapping* p_mapping_read_ind;
  struct os_alloc_mapping* p_mapping_write_ind;
  struct os_alloc_mapping* p_mapping_write_ind_2;
  uint8_t* p_mem_raw;
  uint8_t* p_mem_read;
  uint8_t* p_mem_write;
  uint8_t* p_mem_read_ind;
  uint8_t* p_mem_write_ind;
  uint8_t* p_mem_sideways;
  uint8_t romsel;
  int is_sideways_ram_bank[k_bbc_num_roms];
  int is_extended_rom_addressing;
  int is_romsel_invalidated;
  struct via_struct* p_system_via;
  struct via_struct* p_user_via;
  uint32_t IC32;
  struct keyboard_struct* p_keyboard;
  struct sound_struct* p_sound;
  struct render_struct* p_render;
  struct teletext_struct* p_teletext;
  struct video_struct* p_video;
  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  struct intel_fdc_struct* p_intel_fdc;
  struct serial_struct* p_serial;
  struct tape_struct* p_tape;
  struct cpu_driver* p_cpu_driver;
  struct debug_struct* p_debug;

  /* Timing support. */
  struct os_time_sleeper* p_sleeper;
  uint32_t timer_id_cycles;
  uint32_t timer_id_tick;
  uint32_t timer_id_stop_cycles;
  uint32_t wakeup_rate;
  uint64_t cycles_per_run_fast;
  uint64_t cycles_per_run_normal;
  uint64_t last_time_us;
  uint64_t last_time_us_perf;
  uint64_t last_cycles;
  uint64_t last_frames;
  uint64_t last_crtc_advances;
  uint64_t last_hw_reg_hits;
  uint64_t last_c1;
  uint64_t last_c2;

  uint64_t num_hw_reg_hits;
  int log_speed;
};

static int
bbc_is_always_ram_address(void* p, uint16_t addr) {
  (void) p;

  return (addr < k_bbc_ram_size);
}

static uint16_t
bbc_read_needs_callback_from(void* p) {
  (void) p;

  /* Selects 0xFC00 - 0xFFFF which is broader than the needed 0xFC00 - 0xFEFF,
   * but that's fine.
   */
  return 0xFC00;
}

static uint16_t
bbc_write_needs_callback_from(void* p) {
  (void) p;

  /* TODO: we can do better on Linux: 0xFC00. */
  return k_bbc_os_rom_offset;
}

static int
bbc_read_needs_callback(void* p, uint16_t addr) {
  (void) p;

  if ((addr >= 0xFC00) && (addr < 0xFF00)) {
    return 1;
  }

  return 0;
}

static int
bbc_write_needs_callback(void* p, uint16_t addr) {
  (void) p;

  return (addr >= k_bbc_os_rom_offset);
}

static inline int
bbc_is_1MHz_address(uint16_t addr) {
  if ((addr & 0xFF00) == k_addr_shiela) {
    return k_FE_1mhz_array[((addr >> 5) & 7)];
  }

  if ((addr < k_addr_fred) || (addr > k_addr_shiela_end)) {
    return 0;
  }

  return 1;
}

static void
bbc_do_read_write_tick_handling(struct bbc_struct* p_bbc,
                                uint16_t addr,
                                int do_last_tick_callback) {
  int is_unaligned;
  int do_ticking;
  int is_1MHz = bbc_is_1MHz_address(addr);

  if (!is_1MHz) {
    if (do_last_tick_callback) {
      /* If it's not 1MHz, this is the last tick. */
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    /* Currently, all 2MHz peripherals are handled as tick then access. */
    (void) timing_advance_time_delta(p_bbc->p_timing, 1);
    return;
  }

  is_unaligned = (state_6502_get_cycles(p_bbc->p_state_6502) & 1);

  if (do_last_tick_callback) {
    /* It is 1MHz. Last tick will be in 1 or two ticks depending on
     * alignment.
     */
    /* NOTE: it's not the most efficient to start a one or two tick timer,
     * but it's clean. Furthermore, this is uncommon. The common cases of
     * LDA abs / STA abs for harware register access do not need the last
     * tick callback because the IRQ check occurs before the potentially
     * stretched cycle.
     */
    /* NOTE: there's an important subtlety here. We rely on this timer firing
     * last if there are multiple timers firing at the same this. This
     * guarantee _is_ made by timing API. What happens is that the older
     * timers may raise (or lower) IRQs. And then this newest timer needs to
     * see latest IRQ results to act on them.
     */
    (void) timing_start_timer_with_value(p_bbc->p_timing,
                                         p_bbc->timer_id_tick,
                                         (1 + is_unaligned));
  }

  /* For 1MHz, the specific peripheral callback can opt to take on the timing
   * ticking itself. The VIAs do this. For everything else, we fully tick to
   * the end of the stretched cycle and then do the read or write.
   * It's worth noting that this behavior is required for CRTC. If we fail to
   * tick to the end of the stretched cycle, the writes take effect too soon.
   */
  do_ticking = 1;
  switch (addr & ~0x1F) {
  case k_addr_sysvia:
  case k_addr_uservia:
    do_ticking = 0;
    break;
  default:
    break;
  }

  if (do_ticking) {
    (void) timing_advance_time_delta(p_bbc->p_timing, (2 + is_unaligned));
  }
}

uint8_t
bbc_read_callback(void* p, uint16_t addr, int do_last_tick_callback) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  uint8_t ret = 0xFE;

  p_bbc->num_hw_reg_hits++;

  bbc_do_read_write_tick_handling(p_bbc, addr, do_last_tick_callback);

  switch (addr & ~3) {
  case (k_addr_crtc + 0):
  case (k_addr_crtc + 4):
    {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      ret = video_crtc_read(p_video, (addr & 0x1));
    }
    break;
  case (k_addr_acia + 0):
  case (k_addr_acia + 4):
    ret = serial_acia_read(p_bbc->p_serial, (addr & 1));
    break;
  case (k_addr_serial_ula + 0):
  case (k_addr_serial_ula + 4):
    ret = serial_ula_read(p_bbc->p_serial);
    break;
  case (k_addr_master_adc + 0):
    /* Syncron reads this even on a model B. */
    /* EMU: returns 0 on an issue 3. */
    ret = 0;
    log_do_log(k_log_misc, k_log_unimplemented, "read of $FE18 region");
    break;
  case 0xFE1C:
    /* EMU: a hole here. Returns 0 on an issue 3. */
    ret = 0;
    log_do_log(k_log_misc, k_log_unimplemented, "read of $FE1C region");
    break;
  case (k_addr_video_ula + 0):
  case (k_addr_video_ula + 4):
  case (k_addr_video_ula + 8):
  case (k_addr_video_ula + 12):
    /* EMU NOTE: ULA is write-only, and reads don't seem to be wired up.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=17509
     * Break out to default 0xFE return.
     */
    break;
  case (k_addr_rom_select + 0):
  case (k_addr_rom_select + 4):
  case (k_addr_rom_select + 8):
  case (k_addr_rom_select + 12):
    /* ROMSEL is readable on a Master but not on a model B, so break out to
     * default return.
     */
    break;
  case (k_addr_sysvia + 0):
  case (k_addr_sysvia + 4):
  case (k_addr_sysvia + 8):
  case (k_addr_sysvia + 12):
  case (k_addr_sysvia + 16):
  case (k_addr_sysvia + 20):
  case (k_addr_sysvia + 24):
  case (k_addr_sysvia + 28):
    ret = via_read(p_bbc->p_system_via, (addr & 0xf));
    break;
  case (k_addr_uservia + 0):
  case (k_addr_uservia + 4):
  case (k_addr_uservia + 8):
  case (k_addr_uservia + 12):
  case (k_addr_uservia + 16):
  case (k_addr_uservia + 20):
  case (k_addr_uservia + 24):
  case (k_addr_uservia + 28):
    ret = via_read(p_bbc->p_user_via, (addr & 0xf));
    break;
  case (k_addr_floppy + 0):
  case (k_addr_floppy + 4):
  case (k_addr_floppy + 8):
  case (k_addr_floppy + 12):
  case (k_addr_floppy + 16):
  case (k_addr_floppy + 20):
  case (k_addr_floppy + 24):
  case (k_addr_floppy + 28):
    ret = intel_fdc_read(p_bbc->p_intel_fdc, (addr & 0x7));
    break;
  case (k_addr_econet + 0):
  case (k_addr_econet + 4):
  case (k_addr_econet + 8):
  case (k_addr_econet + 12):
  case (k_addr_econet + 16):
  case (k_addr_econet + 20):
  case (k_addr_econet + 24):
  case (k_addr_econet + 28):
    break;
  case (k_addr_adc + 0):
  case (k_addr_adc + 4):
  case (k_addr_adc + 8):
  case (k_addr_adc + 12):
  case (k_addr_adc + 16):
  case (k_addr_adc + 20):
  case (k_addr_adc + 24):
  case (k_addr_adc + 28):
    switch (addr & 3) {
    case 0: /* Status. */
      /* Return ADC conversion complete (bit 6). */
      ret = 0x40;
      break;
    case 1: /* ADC high. */
      /* Return 0x8000 across high and low, which is "central position" for
       * the joystick.
       */
      ret = 0x80;
      break;
    case 2: /* ADC low. */
      ret = 0;
      break;
    case 3:
      ret = 0;
      log_do_log(k_log_misc, k_log_unimplemented, "ADC read of index 3");
      break;
    }
    break;
  case (k_addr_tube + 0):
  case (k_addr_tube + 4):
  case (k_addr_tube + 8):
  case (k_addr_tube + 12):
  case (k_addr_tube + 16):
  case (k_addr_tube + 20):
  case (k_addr_tube + 24):
  case (k_addr_tube + 28):
    if (p_bbc->test_map_flag) {
      switch (addr) {
      case (k_addr_tube + 1):
        /* &FEE1: read low byte of cycles count. */
        ret = (state_6502_get_cycles(p_bbc->p_state_6502) & 0xFF);
      default:
        break;
      }
    }
    /* Not present. */
    break;
  default:
    if ((addr < k_bbc_registers_start) ||
        (addr >= (k_bbc_registers_start + k_bbc_registers_len))) {
      /* If we miss the registers, it will be:
       * 1) $FF00 - $FFFF on account of trapping on anything above $FC00, or
       * 2) $FBxx on account of the X uncertainty of $FBxx,X, or
       * 3) Temporarily(?) $BFxx on account of $BFxx,X uncertainty and we're
       * trapping $C000+ on all ports (only really needed on Windows). This
       * should only be able to hit with the uncarried address read for
       * something like a page-crossing LDA $BFFF,X
       */
      uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);
      assert(addr >= (k_bbc_os_rom_offset - 0x100));
      ret = p_mem_read[addr];
    } else if (addr >= k_addr_shiela) {
      /* We should have every address covered above. */
      assert(0);
    } else {
      /* EMU: This value, as well as the 0xFE default,  copied from b-em /
       * jsbeeb, and checked against a real BBC, see:
       * https://stardot.org.uk/forums/viewtopic.php?f=4&t=17509
       */
      ret = 0xFF;
    }
    break;
  }

  return ret;
}

uint8_t
bbc_get_romsel(struct bbc_struct* p_bbc) {
  return p_bbc->romsel;
}

static uint8_t
bbc_get_effective_bank(struct bbc_struct* p_bbc, uint8_t romsel) {
  romsel &= 0xF;

  if (!p_bbc->is_extended_rom_addressing) {
    /* EMU NOTE: unless the BBC has a sideways ROM / RAM board installed, all
     * of the 0x0 - 0xF ROMSEL range is aliased into 0xC - 0xF.
     * The STH Castle Quest image needs this due to a misguided "Master
     * compatability" fix.
     * See http://beebwiki.mdfs.net/Paged_ROM.
     */
    romsel &= 0x3;
    romsel += 0xC;
  }

  return romsel;
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
  uint8_t effective_curr_bank;
  uint8_t effective_new_bank;
  int curr_is_ram;
  int new_is_ram;

  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  uint8_t* p_sideways_src = p_bbc->p_mem_sideways;
  uint8_t* p_mem_sideways = (p_bbc->p_mem_raw + k_bbc_sideways_offset);

  effective_curr_bank = bbc_get_effective_bank(p_bbc, p_bbc->romsel);
  effective_new_bank = bbc_get_effective_bank(p_bbc, index);
  assert(!(effective_curr_bank & 0xF0));
  assert(!(effective_new_bank & 0xF0));

  p_bbc->romsel = index;

  if ((effective_new_bank == effective_curr_bank) &&
      !p_bbc->is_romsel_invalidated) {
    return;
  }

  p_bbc->is_romsel_invalidated = 0;

  curr_is_ram = p_bbc->is_sideways_ram_bank[effective_curr_bank];
  new_is_ram = p_bbc->is_sideways_ram_bank[effective_new_bank];

  p_sideways_src += (effective_new_bank * k_bbc_rom_size);

  /* If current bank is RAM, save it. */
  if (curr_is_ram) {
    uint8_t* p_sideways_dest = p_bbc->p_mem_sideways;
    p_sideways_dest += (effective_curr_bank * k_bbc_rom_size);
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
    size_t map_size = (k_6502_addr_space_size * 2);
    size_t half_map_size = (map_size / 2);
    size_t map_offset = (k_6502_addr_space_size / 2);

    os_alloc_free_mapping(p_bbc->p_mapping_write_2);
    os_alloc_free_mapping(p_bbc->p_mapping_write_ind_2);

    if (new_is_ram) {
      intptr_t mem_handle = p_bbc->mem_handle;

      p_bbc->p_mapping_write_2 = os_alloc_get_mapping_from_handle(
          mem_handle,
          (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR + map_offset),
          half_map_size,
          half_map_size);
      os_alloc_make_mapping_none((p_bbc->p_mem_write + k_bbc_os_rom_offset),
                                 k_bbc_rom_size);
      p_bbc->p_mapping_write_ind_2 = os_alloc_get_mapping_from_handle(
          mem_handle,
          (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR + map_offset),
          half_map_size,
          half_map_size);
      os_alloc_make_mapping_none((p_bbc->p_mem_write_ind + k_bbc_os_rom_offset),
                                 k_bbc_rom_size);
    } else {
      p_bbc->p_mapping_write_2 = os_alloc_get_mapping(
          (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR + map_offset),
          half_map_size);
      p_bbc->p_mapping_write_ind_2 = os_alloc_get_mapping(
          (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR + map_offset),
          half_map_size);
    }
    os_alloc_make_mapping_none((p_bbc->p_mem_write + k_6502_addr_space_size),
                               map_offset);
    os_alloc_make_mapping_none(
        (p_bbc->p_mem_write_ind + k_6502_addr_space_size),
        map_offset);
  }
}

void
bbc_write_callback(void* p,
                   uint16_t addr,
                   uint8_t val,
                   int do_last_tick_callback) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  p_bbc->num_hw_reg_hits++;

  bbc_do_read_write_tick_handling(p_bbc, addr, do_last_tick_callback);

  switch (addr & ~3) {
  case (k_addr_crtc + 0):
  case (k_addr_crtc + 4):
    {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      video_crtc_write(p_video, (addr & 0x1), val);
    }
    break;
  case (k_addr_acia + 0):
  case (k_addr_acia + 4):
    serial_acia_write(p_bbc->p_serial, (addr & 0x1), val);
    break;
  case (k_addr_serial_ula + 0):
  case (k_addr_serial_ula + 4):
    serial_ula_write(p_bbc->p_serial, val);
    break;
    break;
  case (k_addr_video_ula + 0):
  case (k_addr_video_ula + 4):
  case (k_addr_video_ula + 8):
  case (k_addr_video_ula + 12):
    {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      video_ula_write(p_video, (addr & 0x1), val);
    }
    break;
  case (k_addr_rom_select + 0):
  case (k_addr_rom_select + 4):
  case (k_addr_rom_select + 8):
  case (k_addr_rom_select + 12):
    bbc_sideways_select(p_bbc, val);
    break;
  case (k_addr_sysvia + 0):
  case (k_addr_sysvia + 4):
  case (k_addr_sysvia + 8):
  case (k_addr_sysvia + 12):
  case (k_addr_sysvia + 16):
  case (k_addr_sysvia + 20):
  case (k_addr_sysvia + 24):
  case (k_addr_sysvia + 28):
    via_write(p_bbc->p_system_via, (addr & 0xf), val);
    break;
  case (k_addr_uservia + 0):
  case (k_addr_uservia + 4):
  case (k_addr_uservia + 8):
  case (k_addr_uservia + 12):
  case (k_addr_uservia + 16):
  case (k_addr_uservia + 20):
  case (k_addr_uservia + 24):
  case (k_addr_uservia + 28):
    via_write(p_bbc->p_user_via, (addr & 0xf), val);
    break;
  case (k_addr_floppy + 0):
  case (k_addr_floppy + 4):
  case (k_addr_floppy + 8):
  case (k_addr_floppy + 12):
  case (k_addr_floppy + 16):
  case (k_addr_floppy + 20):
  case (k_addr_floppy + 24):
  case (k_addr_floppy + 28):
    intel_fdc_write(p_bbc->p_intel_fdc, (addr & 0x7), val);
    break;
  case (k_addr_econet + 0):
    log_do_log(k_log_misc, k_log_unimplemented, "write of ECONET region");
    break;
  case (k_addr_adc + 0):
  case (k_addr_adc + 4):
  case (k_addr_adc + 8):
  case (k_addr_adc + 12):
  case (k_addr_adc + 16):
  case (k_addr_adc + 20):
  case (k_addr_adc + 24):
  case (k_addr_adc + 28):
    /* TODO: ADC, even with nothing connected, isn't correctly emulated yet. */
    break;
  case (k_addr_tube + 0):
  case (k_addr_tube + 4):
  case (k_addr_tube + 8):
  case (k_addr_tube + 12):
  case (k_addr_tube + 16):
  case (k_addr_tube + 20):
  case (k_addr_tube + 24):
  case (k_addr_tube + 28):
    if (p_bbc->test_map_flag) {
      switch (addr) {
      case k_addr_tube:
        /* &FEE0: caush crash. */
        *((volatile uint8_t*) 0xdead) = '\x41';
        break;
      case (k_addr_tube + 1):
        /* &FEE1: reset cycles count. */
        state_6502_set_cycles(p_bbc->p_state_6502, 0);
        break;
      default:
        break;
      }
    } else {
      log_do_log(k_log_misc, k_log_unimplemented, "write of TUBE region");
    }
    break;
  default:
    if ((addr < k_bbc_registers_start) ||
        (addr >= (k_bbc_registers_start + k_bbc_registers_len))) {
      /* If we miss the registers, it will be:
       * 1) $FF00 - $FFFF on account of trapping on anything above $FC00, or
       * 2) $FBxx on account of the X uncertainty of $FBxx,X, or
       * 3) The Windows port needs a wider range to trap ROM writes.
       */
      assert(addr >= k_bbc_os_rom_offset);
    } else if (addr >= k_addr_shiela) {
      /* We should have every address covered above. */
      assert(0);
    }
    break;
  }
}

void
bbc_client_send_message(struct bbc_struct* p_bbc,
                        struct bbc_message* p_message) {
  os_channel_write(p_bbc->handle_channel_write_client,
                   p_message,
                   sizeof(struct bbc_message));
}

static void
bbc_cpu_send_message(struct bbc_struct* p_bbc, struct bbc_message* p_message) {
  os_channel_write(p_bbc->handle_channel_write_bbc,
                   p_message,
                   sizeof(struct bbc_message));
}

void
bbc_client_receive_message(struct bbc_struct* p_bbc,
                           struct bbc_message* p_out_message) {
  os_channel_read(p_bbc->handle_channel_read_client,
                  p_out_message,
                  sizeof(struct bbc_message));
}

static void
bbc_cpu_receive_message(struct bbc_struct* p_bbc,
                        struct bbc_message* p_out_message) {
  os_channel_read(p_bbc->handle_channel_read_bbc,
                  p_out_message,
                  sizeof(struct bbc_message));
}

static void
bbc_framebuffer_ready_callback(void* p,
                               int do_full_render,
                               int framing_changed) {
  struct bbc_message message;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  message.data[0] = k_message_vsync;
  message.data[1] = do_full_render;
  message.data[2] = framing_changed;
  bbc_cpu_send_message(p_bbc, &message);
  if (bbc_get_vsync_wait_for_render(p_bbc)) {
    struct bbc_message message;
    bbc_cpu_receive_message(p_bbc, &message);
    assert(message.data[0] == k_message_render_done);
  }
}

static void
bbc_tick_timer_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  (void) timing_stop_timer(p_bbc->p_timing, p_bbc->timer_id_tick);

  p_bbc->memory_access.memory_client_last_tick_callback(
      p_bbc->memory_access.p_last_tick_callback_obj);
}

struct bbc_struct*
bbc_create(int mode,
           uint8_t* p_os_rom,
           int debug_flag,
           int run_flag,
           int print_flag,
           int fast_flag,
           int accurate_flag,
           int fasttape_flag,
           int test_map_flag,
           const char* p_opt_flags,
           const char* p_log_flags,
           int32_t debug_stop_addr) {
  struct timing_struct* p_timing;
  struct state_6502* p_state_6502;
  struct debug_struct* p_debug;
  uint32_t cpu_scale_factor;
  size_t map_size;
  size_t half_map_size;
  size_t map_offset;

  int externally_clocked_via = 1;
  int externally_clocked_crtc = 1;
  int synchronous_sound = 0;

  struct bbc_struct* p_bbc = util_mallocz(sizeof(struct bbc_struct));

  p_bbc->wakeup_rate = k_bbc_default_wakeup_rate;
  (void) util_get_u32_option(&p_bbc->wakeup_rate,
                             p_opt_flags,
                             "bbc:wakeup-rate=");
  cpu_scale_factor = 1;
  (void) util_get_u32_option(&cpu_scale_factor,
                             p_opt_flags,
                             "bbc:cpu-scale-factor=");

  p_bbc->thread_allocated = 0;
  p_bbc->running = 0;
  p_bbc->p_os_rom = p_os_rom;
  p_bbc->is_extended_rom_addressing = 0;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;
  p_bbc->fast_flag = fast_flag;
  p_bbc->test_map_flag = test_map_flag;
  p_bbc->vsync_wait_for_render = 1;
  p_bbc->exit_value = 0;
  p_bbc->handle_channel_read_bbc = -1;
  p_bbc->handle_channel_write_bbc = -1;
  p_bbc->handle_channel_read_client = -1;
  p_bbc->handle_channel_write_client = -1;

  if (util_has_option(p_opt_flags, "video:no-vsync-wait-for-render")) {
    p_bbc->vsync_wait_for_render = 0;
  }

  p_bbc->p_sleeper = os_time_create_sleeper();
  p_bbc->last_time_us = 0;
  p_bbc->last_time_us_perf = 0;
  p_bbc->last_cycles = 0;
  p_bbc->last_frames = 0;
  p_bbc->last_crtc_advances = 0;
  p_bbc->last_hw_reg_hits = 0;
  p_bbc->num_hw_reg_hits = 0;
  p_bbc->log_speed = util_has_option(p_log_flags, "perf:speed");

  /* We allocate 2 times the 6502 64k address space size. This is so we can
   * place it in the middle of a 128k region and straddle a 64k boundary in the
   * middle. We need that for a satisfactory mappings setup on Windows, which
   * only has 64k allocation resolution. This way, the stuff that is usually
   * RAM is below the boundary and the stuff that is usually ROM is above the
   * boundary, permitting a high performance setup.
   */
  map_size = (k_6502_addr_space_size * 2);
  half_map_size = (map_size / 2);
  map_offset = (k_6502_addr_space_size / 2);
  p_bbc->mem_handle = os_alloc_get_memory_handle(map_size);
  if (p_bbc->mem_handle < 0) {
    util_bail("os_alloc_get_memory_handle failed");
  }

  p_bbc->is_64k_mappings = os_alloc_get_is_64k_mappings();

  p_bbc->p_mapping_raw =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_RAW_ADDR - map_offset),
          0,
          map_size);
  p_bbc->p_mem_raw = (os_alloc_get_mapping_addr(p_bbc->p_mapping_raw) +
                      map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_raw - map_offset), map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_raw + k_6502_addr_space_size),
                             map_offset);

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
  p_bbc->p_mapping_read_ind =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_READ_IND_ADDR - map_offset),
          0,
          map_size);
  p_bbc->p_mem_read_ind =
      (os_alloc_get_mapping_addr(p_bbc->p_mapping_read_ind) + map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_read_ind - map_offset), map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_read_ind + k_6502_addr_space_size),
                             map_offset);
  p_bbc->p_mapping_write_ind =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR - map_offset),
          0,
          half_map_size);
  /* Writeable dummy ROM region. */
  p_bbc->p_mapping_write_ind_2 =
      os_alloc_get_mapping(
          (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR + map_offset),
          half_map_size);
  p_bbc->p_mem_write_ind =
      (os_alloc_get_mapping_addr(p_bbc->p_mapping_write_ind) + map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_write_ind - map_offset), map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_write_ind + k_6502_addr_space_size),
                             map_offset);

  p_bbc->p_mapping_read =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_READ_FULL_ADDR - map_offset),
          0,
          map_size);
  p_bbc->p_mem_read = (os_alloc_get_mapping_addr(p_bbc->p_mapping_read) +
                       map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_read - map_offset), map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_read + k_6502_addr_space_size),
                             map_offset);
  p_bbc->p_mapping_write =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR - map_offset),
          0,
          half_map_size);
  /* Writeable dummy ROM region. */
  p_bbc->p_mapping_write_2 =
      os_alloc_get_mapping(
          (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR + map_offset),
          half_map_size);
  p_bbc->p_mem_write = (os_alloc_get_mapping_addr(p_bbc->p_mapping_write) +
                        map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_write - map_offset), map_offset);
  os_alloc_make_mapping_none((p_bbc->p_mem_write + k_6502_addr_space_size),
                             map_offset);

  p_bbc->p_mem_sideways = util_mallocz(k_bbc_rom_size * k_bbc_num_roms);

  /* TODO: we can widen what we make read-only? */
  /* Make the ROM readonly in the read mappings used at runtime. */
  os_alloc_make_mapping_read_only((p_bbc->p_mem_read + k_bbc_ram_size),
                                  (k_6502_addr_space_size - k_bbc_ram_size));
  os_alloc_make_mapping_read_only((p_bbc->p_mem_read_ind + k_bbc_ram_size),
                                  (k_6502_addr_space_size - k_bbc_ram_size));

  /* Make the registers page inaccessible in the indirect read / write
   * mappings. This enables an optimization: indirect reads and writes can
   * avoid expensive checks for hitting registers, which is rare, and rely
   * instead on a fault + fixup.
   */
  os_alloc_make_mapping_none(
      (p_bbc->p_mem_read_ind + K_BBC_MEM_INACCESSIBLE_OFFSET),
      K_BBC_MEM_INACCESSIBLE_LEN);
  os_alloc_make_mapping_none(
      (p_bbc->p_mem_write_ind + K_BBC_MEM_INACCESSIBLE_OFFSET),
      K_BBC_MEM_INACCESSIBLE_LEN);

  p_bbc->memory_access.p_mem_read = p_bbc->p_mem_read;
  p_bbc->memory_access.p_mem_write = p_bbc->p_mem_write;
  p_bbc->memory_access.p_callback_obj = p_bbc;
  p_bbc->memory_access.memory_is_always_ram = bbc_is_always_ram_address;
  p_bbc->memory_access.memory_read_needs_callback_from =
      bbc_read_needs_callback_from;
  p_bbc->memory_access.memory_write_needs_callback_from =
      bbc_write_needs_callback_from;
  p_bbc->memory_access.memory_read_needs_callback = bbc_read_needs_callback;
  p_bbc->memory_access.memory_write_needs_callback = bbc_write_needs_callback;
  p_bbc->memory_access.memory_read_callback = bbc_read_callback;
  p_bbc->memory_access.memory_write_callback = bbc_write_callback;

  p_bbc->options.debug_subsystem_active = debug_subsystem_active;
  p_bbc->options.debug_active_at_addr = debug_active_at_addr;
  p_bbc->options.debug_callback = debug_callback;
  p_bbc->options.p_opt_flags = p_opt_flags;
  p_bbc->options.p_log_flags = p_log_flags;

  /* Accurate mode is implied if fast mode isn't selected. */
  if (!fast_flag) {
    accurate_flag = 1;
  }
  p_bbc->options.accurate = accurate_flag;

  if (accurate_flag) {
    externally_clocked_via = 0;
    externally_clocked_crtc = 0;
    synchronous_sound = 1;
  }

  p_timing = timing_create(cpu_scale_factor);
  if (p_timing == NULL) {
    util_bail("timing_create failed");
  }
  p_bbc->p_timing = p_timing;
  p_bbc->timer_id_tick = timing_register_timer(p_timing,
                                               bbc_tick_timer_callback,
                                               p_bbc);

  p_state_6502 = state_6502_create(p_timing, p_bbc->p_mem_read);
  if (p_state_6502 == NULL) {
    util_bail("state_6502_create failed");
  }
  p_bbc->p_state_6502 = p_state_6502;

  p_bbc->p_system_via = via_create(k_via_system,
                                   externally_clocked_via,
                                   p_timing,
                                   p_bbc);
  if (p_bbc->p_system_via == NULL) {
    util_bail("via_create failed");
  }
  p_bbc->p_user_via = via_create(k_via_user,
                                 externally_clocked_via,
                                 p_timing,
                                 p_bbc);
  if (p_bbc->p_user_via == NULL) {
    util_bail("via_create failed");
  }

  p_bbc->p_keyboard = keyboard_create(p_timing,
                                      p_bbc->p_system_via,
                                      p_state_6502);
  if (p_bbc->p_keyboard == NULL) {
    util_bail("keyboard_create failed");
  }

  p_bbc->p_sound = sound_create(synchronous_sound, p_timing, &p_bbc->options);
  if (p_bbc->p_sound == NULL) {
    util_bail("sound_create failed");
  }

  p_bbc->p_teletext = teletext_create();
  if (p_bbc->p_teletext == NULL) {
    util_bail("teletext_create failed");
  }
  p_bbc->p_render = render_create(p_bbc->p_teletext, &p_bbc->options);
  if (p_bbc->p_render == NULL) {
    util_bail("render_create failed");
  }
  p_bbc->p_video = video_create(p_bbc->p_mem_read,
                                externally_clocked_crtc,
                                p_timing,
                                p_bbc->p_render,
                                p_bbc->p_teletext,
                                p_bbc->p_system_via,
                                bbc_framebuffer_ready_callback,
                                p_bbc,
                                &p_bbc->fast_flag,
                                &p_bbc->options);
  if (p_bbc->p_video == NULL) {
    util_bail("video_create failed");
  }

  p_bbc->p_drive_0 = disc_drive_create(p_timing);
  if (p_bbc->p_drive_0 == NULL) {
    util_bail("disc_drive_create failed");
  }
  p_bbc->p_drive_1 = disc_drive_create(p_timing);
  if (p_bbc->p_drive_1 == NULL) {
    util_bail("disc_drive_create failed");
  }

  p_bbc->p_intel_fdc = intel_fdc_create(p_state_6502, &p_bbc->options);
  if (p_bbc->p_intel_fdc == NULL) {
    util_bail("intel_fdc_create failed");
  }
  intel_fdc_set_drives(p_bbc->p_intel_fdc, p_bbc->p_drive_0, p_bbc->p_drive_1);

  p_bbc->p_tape = tape_create(p_timing, &p_bbc->options);
  if (p_bbc->p_tape == NULL) {
    util_bail("tape_create failed");
  }

  p_bbc->p_serial = serial_create(p_state_6502,
                                  &p_bbc->fast_flag,
                                  fasttape_flag,
                                  &p_bbc->options);
  if (p_bbc->p_serial == NULL) {
    util_bail("serial_create failed");
  }
  serial_set_tape(p_bbc->p_serial, p_bbc->p_tape);

  p_debug = debug_create(p_bbc, debug_flag, debug_stop_addr);
  if (p_debug == NULL) {
    util_bail("debug_create failed");
  }

  p_bbc->options.p_debug_object = p_debug;

  p_bbc->p_cpu_driver = cpu_driver_alloc(mode,
                                         p_state_6502,
                                         &p_bbc->memory_access,
                                         p_timing,
                                         &p_bbc->options);
  if (p_bbc->p_cpu_driver == NULL) {
    util_bail("cpu_driver_alloc failed");
  }

  bbc_power_on_reset(p_bbc);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  volatile int* p_running = &p_bbc->running;
  volatile int* p_thread_allocated = &p_bbc->thread_allocated;

  (void) p_running;
  assert(!*p_running);

  if (*p_thread_allocated) {
    (void) os_thread_destroy(p_bbc->p_thread_cpu);
  }

  p_cpu_driver->p_funcs->destroy(p_cpu_driver);

  debug_destroy(p_bbc->p_debug);
  serial_destroy(p_bbc->p_serial);
  tape_destroy(p_bbc->p_tape);
  video_destroy(p_bbc->p_video);
  teletext_destroy(p_bbc->p_teletext);
  render_destroy(p_bbc->p_render);
  sound_destroy(p_bbc->p_sound);
  keyboard_destroy(p_bbc->p_keyboard);
  via_destroy(p_bbc->p_system_via);
  via_destroy(p_bbc->p_user_via);
  intel_fdc_destroy(p_bbc->p_intel_fdc);
  disc_drive_destroy(p_bbc->p_drive_0);
  disc_drive_destroy(p_bbc->p_drive_1);
  state_6502_destroy(p_bbc->p_state_6502);
  timing_destroy(p_bbc->p_timing);
  os_alloc_free_mapping(p_bbc->p_mapping_raw);
  os_alloc_free_mapping(p_bbc->p_mapping_read);
  os_alloc_free_mapping(p_bbc->p_mapping_write);
  os_alloc_free_mapping(p_bbc->p_mapping_write_2);
  os_alloc_free_mapping(p_bbc->p_mapping_read_ind);
  os_alloc_free_mapping(p_bbc->p_mapping_write_ind);
  os_alloc_free_mapping(p_bbc->p_mapping_write_ind_2);
  os_alloc_free_memory_handle(p_bbc->mem_handle);

  os_time_free_sleeper(p_bbc->p_sleeper);

  util_free(p_bbc->p_mem_sideways);
  util_free(p_bbc);
}

void
bbc_load_rom(struct bbc_struct* p_bbc,
             uint8_t index,
             uint8_t* p_rom_src) {
  uint8_t* p_rom_dest = p_bbc->p_mem_sideways;

  assert(index < k_bbc_num_roms);

  p_rom_dest += (index * k_bbc_rom_size);
  (void) memcpy(p_rom_dest, p_rom_src, k_bbc_rom_size);
  if (index < 0xC) {
    p_bbc->is_extended_rom_addressing = 1;
  }
  p_bbc->is_romsel_invalidated = 1;
  bbc_sideways_select(p_bbc, p_bbc->romsel);
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
  if (index < 0xC) {
    p_bbc->is_extended_rom_addressing = 1;
  }
  p_bbc->is_romsel_invalidated = 1;
  bbc_sideways_select(p_bbc, p_bbc->romsel);
}

void
bbc_power_on_reset(struct bbc_struct* p_bbc) {
  uint8_t* p_mem_raw = p_bbc->p_mem_raw;
  uint8_t* p_os_start = (p_mem_raw + k_bbc_os_rom_offset);
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);

  /* Clear memory. */
  /* EMU NOTE: skullduggery! On a lot of BBCs, the power-on DRAM state is 0xFF,
   * and Eagle Empire was even found to depend on this otherwise it is crashy.
   * On the flipside, Clogger appears to rely on a power-on DRAM value of 0x00
   * in the zero page.
   * We cater for both of these quirks below.
   * Full story: https://github.com/mattgodbolt/jsbeeb/issues/105
   */
  (void) memset(p_mem_raw, '\xFF', k_6502_addr_space_size);
  (void) memset(p_mem_raw, '\0', 0x100);

  /* Copy in OS ROM. */
  (void) memcpy(p_os_start, p_bbc->p_os_rom, k_bbc_rom_size);

  p_bbc->is_romsel_invalidated = 1;
  bbc_sideways_select(p_bbc, 0);

  state_6502_reset(p_state_6502);
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

struct render_struct*
bbc_get_render(struct bbc_struct* p_bbc) {
  return p_bbc->p_render;
}

struct serial_struct*
bbc_get_serial(struct bbc_struct* p_bbc) {
  return p_bbc->p_serial;
}

struct timing_struct*
bbc_get_timing(struct bbc_struct* p_bbc) {
  return p_bbc->p_timing;
}

uint8_t
bbc_get_IC32(struct bbc_struct* p_bbc) {
  return p_bbc->IC32;
}

void
bbc_set_IC32(struct bbc_struct* p_bbc, uint8_t val) {
  p_bbc->IC32 = val;

  /* IC32 is the addressable latch. It selects what peripheral(s) are active on
   * the system VIA port A bus, as well as storing other system bits.
   * Changing this selection requires various notifications.
   */

  /* Selecting or deselecting the keyboard may need to change interrupt and / or
   * bus value status.
   */
  via_update_port_a(p_bbc->p_system_via);

  /* The video ULA needs to know about changes to the video wrap-around
   * address.
   */
  video_IC32_updated(p_bbc->p_video, val);
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
bbc_do_sleep(struct bbc_struct* p_bbc,
             uint64_t last_time_us,
             uint64_t curr_time_us,
             uint64_t delta_us) {
  uint64_t next_wakeup_time_us;
  int64_t spare_time_us;

  struct sound_struct* p_sound = p_bbc->p_sound;

  /* If we're synchronously writing to the sound driver at the same time the
   * CPU executes, the timing is locked to the blocking sound driver write.
   */
  if (sound_is_active(p_sound) && sound_is_synchronous(p_sound)) {
    return;
  }

  next_wakeup_time_us = (last_time_us + delta_us);
  spare_time_us = (next_wakeup_time_us - curr_time_us);

  p_bbc->last_time_us = next_wakeup_time_us;

  if (spare_time_us >= 0) {
    os_time_sleeper_sleep_us(p_bbc->p_sleeper, spare_time_us);
  } else {
    /* Missed a tick.
     * In all cases, don't sleep.
     */
     if (spare_time_us >= -20000) {
       /* If it's a small miss, keep the existing timing expectations so that
        * virtual time can catch up to wall time.
        */
     } else {
       /* If it's a large miss, perhaps the system was paused in the
        * debugger or some other good reason, so reset timing expectations
        * to avoid a huge flurry of catch-up at 100% CPU.
        */
       p_bbc->last_time_us = curr_time_us;
     }
  }
}

static void
bbc_do_log_speed(struct bbc_struct* p_bbc, uint64_t curr_time_us) {
  uint64_t curr_cycles;
  uint64_t curr_frames;
  uint64_t curr_crtc_advances;
  uint64_t curr_hw_reg_hits;
  uint64_t curr_c1;
  uint64_t curr_c2;
  uint64_t delta_cycles;
  uint64_t delta_frames;
  uint64_t delta_crtc_advances;
  uint64_t delta_hw_reg_hits;
  uint64_t delta_c1;
  uint64_t delta_c2;
  double delta_s;
  double fps;
  double mhz;
  double crtc_ps;
  double hw_reg_ps;
  double c1_ps;
  double c2_ps;

  struct video_struct* p_video = p_bbc->p_video;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  if (p_bbc->last_time_us_perf == 0) {
    p_bbc->last_time_us_perf = curr_time_us;
    return;
  }
  if (curr_time_us < (p_bbc->last_time_us_perf + 1000000)) {
    return;
  }

  curr_cycles = timing_get_total_timer_ticks(p_bbc->p_timing);
  curr_frames = video_get_num_vsyncs(p_video);
  curr_crtc_advances = video_get_num_crtc_advances(p_video);
  curr_hw_reg_hits = p_bbc->num_hw_reg_hits;
  p_cpu_driver->p_funcs->get_custom_counters(p_cpu_driver, &curr_c1, &curr_c2);

  delta_cycles = (curr_cycles - p_bbc->last_cycles);
  delta_frames = (curr_frames - p_bbc->last_frames);
  delta_crtc_advances = (curr_crtc_advances - p_bbc->last_crtc_advances);
  delta_hw_reg_hits = (curr_hw_reg_hits - p_bbc->last_hw_reg_hits);
  delta_s = ((curr_time_us - p_bbc->last_time_us_perf) / 1000000.0);
  delta_c1 = (curr_c1 - p_bbc->last_c1);
  delta_c2 = (curr_c2 - p_bbc->last_c2);

  fps = (delta_frames / delta_s);
  mhz = ((delta_cycles / delta_s) / 1000000.0);
  crtc_ps = (delta_crtc_advances / delta_s);
  hw_reg_ps = (delta_hw_reg_hits / delta_s);
  c1_ps = (delta_c1 / delta_s);
  c2_ps = (delta_c2 / delta_s);

  log_do_log(k_log_perf,
             k_log_info,
             " %.1f fps, %.1f Mhz, %.1f crtc/s %.1f hw/s %.1f c1/s %.1f c2/s",
             fps,
             mhz,
             crtc_ps,
             hw_reg_ps,
             c1_ps,
             c2_ps);

  p_bbc->last_cycles = curr_cycles;
  p_bbc->last_frames = curr_frames;
  p_bbc->last_crtc_advances = curr_crtc_advances;
  p_bbc->last_hw_reg_hits = curr_hw_reg_hits;
  p_bbc->last_time_us_perf = curr_time_us;
  p_bbc->last_c1 = curr_c1;
  p_bbc->last_c2 = curr_c2;
}

static void
bbc_cycles_timer_callback(void* p) {
  uint64_t delta_us;
  uint64_t cycles_next_run;
  int64_t refreshed_time;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct timing_struct* p_timing = p_bbc->p_timing;
  struct keyboard_struct* p_keyboard = p_bbc->p_keyboard;
  struct sound_struct* p_sound = p_bbc->p_sound;
  uint64_t curr_time_us = os_time_get_us();
  uint64_t last_time_us = p_bbc->last_time_us;
  int is_replay = keyboard_is_replaying(p_keyboard);

  /* Pull physical key events from system thread, always.
   * If this ends up updating the virtual keyboard, this call also syncs
   * interrupts and checks for BREAK.
   */
  keyboard_read_queue(p_keyboard);

  /* Bit of a special case, but break out of fast mode if replay hits EOF. */
  if (keyboard_consume_had_replay_eof(p_keyboard)) {
    p_bbc->fast_flag = 0;
  }

  /* Check for special alt key combos to change emulator behavior. */
  if (keyboard_consume_alt_key_press(p_keyboard, 'F')) {
    /* Toggle fast mode. */
    p_bbc->fast_flag = !p_bbc->fast_flag;
    sound_set_output_enabled(p_sound, !p_bbc->fast_flag);
  } else if (keyboard_consume_alt_key_press(p_keyboard, 'E')) {
    /* Exit any in progress replay. */
    if (is_replay) {
      keyboard_end_replay(p_keyboard);
    }
  }

  p_bbc->last_time_us = curr_time_us;

  if (!p_bbc->fast_flag) {
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
    cycles_next_run = p_bbc->cycles_per_run_normal;
    delta_us = (1000000 / p_bbc->wakeup_rate);

    /* This may adjust p_bbc->last_time_us to maintain smooth timing. */
    bbc_do_sleep(p_bbc, last_time_us, curr_time_us, delta_us);
  } else {
    /* Fast mode.
     * Fast mode is where the system executes as fast as the host CPU can
     * manage. Host CPU usage for the system's main thread will be 100%.
     * Effective system CPU rates of many GHz are likely to be obtained.
     */
    cycles_next_run = p_bbc->cycles_per_run_fast;
    /* TODO: limit delta_us max size in case system was paused? */
    delta_us = (curr_time_us - last_time_us);
  }

  (void) timing_adjust_timer_value(p_timing,
                                   &refreshed_time,
                                   p_bbc->timer_id_cycles,
                                   cycles_next_run);

  assert(refreshed_time > 0);

  /* Provide the wall time delta to various modules.
   * In inaccurate modes, this wall time may be used to advance state.
   */
  via_apply_wall_time_delta(p_bbc->p_system_via, delta_us);
  via_apply_wall_time_delta(p_bbc->p_user_via, delta_us);
  video_apply_wall_time_delta(p_bbc->p_video, delta_us);

  /* Prod the sound module in case it's in synchronous mode. */
  sound_tick(p_sound);

  /* TODO: this is pretty poor. The serial device should maintain its own
   * timer at the correct baud rate for the externally attached device.
   */
  serial_tick(p_bbc->p_serial);

  if (p_bbc->log_speed) {
    bbc_do_log_speed(p_bbc, curr_time_us);
  }
}

static void
bbc_start_timer_tick(struct bbc_struct* p_bbc) {
  uint32_t option_cycles_per_run;
  uint64_t speed;

  struct timing_struct* p_timing = p_bbc->p_timing;

  p_bbc->timer_id_cycles = timing_register_timer(p_timing,
                                                 bbc_cycles_timer_callback,
                                                 p_bbc);

  /* Normal mode is when the system is running at real time, aka. "slow" mode.
   * Fast mode is when the system is running the CPU as fast as possible.
   * In fast mode, peripherals may run either in real time, or synced with
   * fast CPU.
   */
  p_bbc->cycles_per_run_normal = (k_bbc_tick_rate / p_bbc->wakeup_rate);

  /* For fast mode, estimate how fast the CPU really is (based on JIT mode,
   * debug mode, etc.) And then arrange to check in at approximately every
   * p_bbc->wakeup_rate. This will ensure reasonable timer resolution and
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

  p_bbc->cycles_per_run_fast = (speed / p_bbc->wakeup_rate);

  if (util_get_u32_option(&option_cycles_per_run,
                          p_bbc->options.p_opt_flags,
                          "bbc:cycles-per-run=")) {
    p_bbc->cycles_per_run_fast = option_cycles_per_run;
  }

  (void) timing_start_timer_with_value(p_timing, p_bbc->timer_id_cycles, 1);

  p_bbc->last_time_us = os_time_get_us();
}

static void*
bbc_cpu_thread(void* p) {
  int exited;
  struct bbc_message message;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  bbc_start_timer_tick(p_bbc);

  exited = p_cpu_driver->p_funcs->enter(p_cpu_driver);
  (void) exited;
  assert(exited == 1);
  assert(p_cpu_driver->p_funcs->has_exited(p_cpu_driver) == 1);

  p_bbc->running = 0;
  p_bbc->exit_value = p_cpu_driver->p_funcs->get_exit_value(p_cpu_driver);

  message.data[0] = k_message_exited;
  bbc_cpu_send_message(p_bbc, &message);

  return NULL;
}

void
bbc_run_async(struct bbc_struct* p_bbc) {
  p_bbc->p_thread_cpu = os_thread_create(bbc_cpu_thread, p_bbc);

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

void
bbc_set_channel_handles(struct bbc_struct* p_bbc,
                        intptr_t handle_channel_read_bbc,
                        intptr_t handle_channel_write_bbc,
                        intptr_t handle_channel_read_client,
                        intptr_t handle_channel_write_client) {
  p_bbc->handle_channel_read_bbc = handle_channel_read_bbc;
  p_bbc->handle_channel_write_bbc = handle_channel_write_bbc;
  p_bbc->handle_channel_read_client = handle_channel_read_client;
  p_bbc->handle_channel_write_client = handle_channel_write_client;
}

void
bbc_load_disc(struct bbc_struct* p_bbc,
              const char* p_filename,
              int drive,
              int is_writeable,
              int is_mutable,
              int convert_to_hfe) {
  struct disc_drive_struct* p_drive;
  struct disc_struct* p_disc;

  assert((drive >= 0) && (drive <= 1));

  if (drive == 0) {
    p_drive = p_bbc->p_drive_0;
  } else {
    p_drive = p_bbc->p_drive_1;
  }

  p_disc = disc_create(p_filename,
                       is_writeable,
                       is_mutable,
                       convert_to_hfe,
                       &p_bbc->options);
  if (p_disc == NULL) {
    util_bail("disc_create failed");
  }

  disc_drive_add_disc(p_drive, p_disc);
}

void
bbc_load_tape(struct bbc_struct* p_bbc, const char* p_file_name) {
  tape_load(p_bbc->p_tape, p_file_name);
}

static void
bbc_stop_cycles_timer_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  (void) timing_stop_timer(p_bbc->p_timing, p_bbc->timer_id_stop_cycles);

  p_cpu_driver->p_funcs->exit(p_cpu_driver, 0xFFFFFFFE);
}

void
bbc_set_stop_cycles(struct bbc_struct* p_bbc, uint64_t cycles) {
  struct timing_struct* p_timing = p_bbc->p_timing;
  uint32_t id = timing_register_timer(p_bbc->p_timing,
                                      bbc_stop_cycles_timer_callback,
                                      p_bbc);
  p_bbc->timer_id_stop_cycles = id;
  (void) timing_start_timer_with_value(p_timing, id, cycles);
}
