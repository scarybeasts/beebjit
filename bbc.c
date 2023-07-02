#include "bbc.h"

#include "adc.h"
#include "bbc_options.h"
#include "cmos.h"
#include "cpu_driver.h"
#include "debug.h"
#include "defs_6502.h"
#include "disc.h"
#include "disc_drive.h"
#include "intel_fdc.h"
#include "joystick.h"
#include "keyboard.h"
#include "log.h"
#include "mc6850.h"
#include "memory_access.h"
#include "os_alloc.h"
#include "os_channel.h"
#include "os_thread.h"
#include "os_time.h"
#include "render.h"
#include "serial_ula.h"
#include "sound.h"
#include "state_6502.h"
#include "tape.h"
#include "teletext.h"
#include "timing.h"
#include "util.h"
#include "via.h"
#include "video.h"
#include "wd_fdc.h"

#include "asm/asm_defs_host.h"
/* For asm_jit_uses_indirect_mappings(). */
#include "asm/asm_jit.h"
#include "asm/asm_opcodes.h"
#include "asm/asm_util.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

static const size_t k_bbc_os_rom_offset = 0xC000;
static const size_t k_bbc_sideways_offset = 0x8000;
static const size_t k_bbc_shadow_offset = 0x3000;
static const size_t k_bbc_lynne_size = 0x5000;
static const size_t k_bbc_hazel_size = 0x2000;
static const size_t k_bbc_andy_size = 0x1000;

static const size_t k_bbc_tick_rate = 2000000; /* 2Mhz. */
static const size_t k_bbc_default_wakeup_rate = 500; /* 2ms / 500Hz. */

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
  /* &FE30 - &FE3F on model B. */
  k_addr_master_floppy  = 0xFE24,
  /* &FE24 - &FE2F. */
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

enum {
  k_romsel_andy = 0x80,
};

enum {
  k_acccon_display_lynne = 0x01,
  k_acccon_access_lynne_from_os = 0x02,
  k_acccon_lynne = 0x04,
  k_acccon_hazel = 0x08,
};

struct bbc_struct {
  /* Fields referenced by JIT encoded callbacks. */
  struct timing_struct* p_timing;
  struct via_struct* p_system_via;
  struct via_struct* p_user_via;
  void* p_via_read_T1CH_func;
  void* p_via_read_T2CH_func;
  void* p_via_read_ORAnh_func;
  void* p_via_write_ORB_func;
  void* p_via_write_DDRA_func;
  void* p_via_write_IFR_func;
  void* p_via_write_ORAnh_func;
  struct adc_struct* p_adc;
  void* p_adc_write_func;
  struct mc6850_struct* p_serial;
  struct intel_fdc_struct* p_intel_fdc;
  struct video_struct* p_video;
  void* p_video_write_func;

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
  uint64_t rewind_to_cycles;
  uint32_t log_count_shadow_speed;
  uint32_t log_count_misc_unimplemented;
  struct util_file* p_printer_file;

  /* Machine configuration. */
  int is_master;
  int is_sideways_ram_bank[k_bbc_num_roms];
  int is_extended_rom_addressing;
  int is_wd_fdc;
  int is_wd_1772;

  /* Settings. */
  uint8_t* p_os_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  int fast_flag;
  int test_map_flag;
  int autoboot_flag;
  int do_video_memory_sync;
  struct bbc_options options;
  int is_compat_old_1MHz_cycles;

  /* Machine state. */
  struct state_6502* p_state_6502;
  struct memory_access memory_access;
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
  uint8_t* p_mem_master;
  uint8_t* p_mem_lynne;
  uint8_t* p_mem_hazel;
  uint8_t* p_mem_andy;
  uint8_t romsel;
  uint8_t acccon;
  int is_acccon_usr_mos_different;
  int is_romsel_invalidated;
  uint16_t read_callback_from;
  uint16_t write_callback_from;
  uint32_t IC32;
  struct keyboard_struct* p_keyboard;
  struct joystick_struct* p_joystick;
  struct sound_struct* p_sound;
  struct render_struct* p_render;
  struct teletext_struct* p_teletext;
  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  struct wd_fdc_struct* p_wd_fdc;
  struct serial_ula_struct* p_serial_ula;
  struct tape_struct* p_tape;
  struct cmos_struct* p_cmos;
  struct cpu_driver* p_cpu_driver;
  struct debug_struct* p_debug;

  /* Timing support. */
  struct os_time_sleeper* p_sleeper;
  uint32_t timer_id_cycles;
  uint32_t timer_id_stop_cycles;
  int32_t timer_id_autoboot;
  int32_t timer_id_test_nmi;
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
  uint64_t cycle_count_baseline;

  uint64_t num_hw_reg_hits;
  int log_speed;
  int log_timestamp;
};

static int
bbc_is_always_ram_address(void* p, uint16_t addr) {
  (void) p;

  return (addr < k_bbc_ram_size);
}

static uint16_t
bbc_read_needs_callback_from(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  return p_bbc->read_callback_from;
}

static uint16_t
bbc_write_needs_callback_from(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  return p_bbc->write_callback_from;
}

static int
bbc_read_needs_callback(void* p, uint16_t addr) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  (void) p_bbc;
  assert(!p_bbc->is_master);

  if ((addr >= 0xFC00) && (addr < 0xFF00)) {
    return 1;
  }

  return 0;
}

static int
bbc_write_needs_callback(void* p, uint16_t addr) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  (void) p_bbc;
  assert(!p_bbc->is_master);

  return (addr >= k_bbc_os_rom_offset);
}

static inline int
bbc_is_1MHz_address(struct bbc_struct* p_bbc, uint16_t addr) {
  if ((addr & 0xFF00) == k_addr_shiela) {
    if (p_bbc->is_master) {
      /* See https://stardot.org.uk/forums/viewtopic.php?f=4&t=16114#p221935,
       * thanks Tom Seddon!
       */
      if ((addr > 0xFEA0) ||
          ((addr >= 0xFE20) && (addr < 0xFE28)) ||
          ((addr >= 0xFE2C) && (addr < 0xFE40))) {
        return 0;
      }
      return 1;
    } else {
      return k_FE_1mhz_array[((addr >> 5) & 7)];
    }
  }

  if ((addr < k_addr_fred) || (addr > k_addr_shiela_end)) {
    return 0;
  }

  return 1;
}

static void
bbc_do_pre_read_write_tick_handling(struct bbc_struct* p_bbc,
                                    uint16_t addr,
                                    uint64_t cycles,
                                    int do_last_tick_callback) {
  uint64_t alignment;

  int is_1MHz = bbc_is_1MHz_address(p_bbc, addr);

  if (!is_1MHz) {
    if (do_last_tick_callback) {
      /* If it's not 1MHz, this is the last tick. */
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    /* Currently, all 2MHz peripherals are handled as tick then access, except
     * the video ULA.
     */
    if ((addr & ~0x000F) == k_addr_video_ula) {
      if (!p_bbc->is_master || (addr < (k_addr_video_ula + 4))) {
        /* Access then tick. */
        return;
      }
    }
    (void) timing_advance_time_delta(p_bbc->p_timing, 1);
    return;
  }

  /* For 1MHz, the specific peripheral handling can opt to take on the timing
   * ticking itself. The VIAs do this.
   */
  if ((addr >= k_addr_sysvia) && (addr < k_addr_floppy)) {
    return;
  }

  /* It is 1MHz. Last tick will be in 1 or two ticks depending on alignment. */
  alignment = (cycles & 1);

  /* For most peripherals, we tick to the end of the stretched cycle and then do   * the read or write.
   * It's worth noting that this behavior is required for CRTC. If we fail to
   * tick to the end of the stretched cycle, the writes take effect too soon.
   */
  if (do_last_tick_callback) {
    (void) timing_advance_time_delta(p_bbc->p_timing, (alignment + 1));
    p_bbc->memory_access.memory_client_last_tick_callback(
        p_bbc->memory_access.p_last_tick_callback_obj);
    (void) timing_advance_time_delta(p_bbc->p_timing, 1);
  } else {
    (void) timing_advance_time_delta(p_bbc->p_timing, (alignment + 2));
  }
}

static inline uint8_t
bbc_do_master_ram_read(struct bbc_struct* p_bbc, uint16_t addr, uint16_t pc) {
  uint8_t* p_mem_raw = p_bbc->p_mem_raw;
  if (addr < k_bbc_sideways_offset) {
    assert(addr >= k_bbc_shadow_offset);
    assert(p_bbc->is_acccon_usr_mos_different);
    if ((pc > k_bbc_os_rom_offset) &&
        (pc <= (k_bbc_os_rom_offset + k_bbc_hazel_size))) {
      return p_bbc->p_mem_master[addr];
    }
  }

  return p_mem_raw[addr];
}

static inline void
bbc_do_master_ram_write(struct bbc_struct* p_bbc,
                        uint16_t addr,
                        uint8_t val,
                        uint16_t pc) {
  uint8_t* p_mem_raw = p_bbc->p_mem_raw;
  if (addr < k_bbc_sideways_offset) {
    assert(addr >= k_bbc_shadow_offset);
    assert(p_bbc->is_acccon_usr_mos_different);
    if ((pc > k_bbc_os_rom_offset) &&
        (pc <= (k_bbc_os_rom_offset + k_bbc_hazel_size))) {
      p_bbc->p_mem_master[addr] = val;
    } else {
      p_bbc->p_mem_raw[addr] = val;
    }
    return;
  }
  /* Sideways region is writeable is RAM is paged in. This is true regardless
   * of whether ANDY is paged in or not.
   */
  if ((addr < k_bbc_os_rom_offset) &&
      p_bbc->is_sideways_ram_bank[p_bbc->romsel & 0x0F]) {
    p_mem_raw[addr] = val;
    return;
  }
  /* ANDY is writeable if paged in. */
  if ((addr < (k_bbc_sideways_offset + k_bbc_andy_size)) &&
      (p_bbc->romsel & k_romsel_andy)) {
    p_mem_raw[addr] = val;
    return;
  }
  /* HAZEL is writeable if paged in. */
  if ((addr >= k_bbc_os_rom_offset) &&
      (addr < (k_bbc_os_rom_offset + k_bbc_hazel_size)) &&
      (p_bbc->acccon & k_acccon_hazel)) {
    p_mem_raw[addr] = val;
    return;
  }
}

uint8_t
bbc_read_callback(void* p,
                  uint16_t addr,
                  uint16_t pc,
                  int do_last_tick_callback) {
  uint8_t ret;
  uint64_t cycles;
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct timing_struct* p_timing = p_bbc->p_timing;

  if (p_bbc->is_compat_old_1MHz_cycles) {
    cycles = state_6502_get_cycles(p_bbc->p_state_6502);
  } else {
    cycles = timing_get_total_timer_ticks(p_timing);
  }

  bbc_do_pre_read_write_tick_handling(p_bbc,
                                      addr,
                                      cycles,
                                      do_last_tick_callback);

  if (p_bbc->is_master && (addr < k_addr_fred)) {
    return bbc_do_master_ram_read(p_bbc, addr, pc);
  }

  ret = 0xFE;
  p_bbc->num_hw_reg_hits++;

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
    ret = mc6850_read(p_bbc->p_serial, (addr & 1));
    break;
  case (k_addr_serial_ula + 0):
  case (k_addr_serial_ula + 4):
    ret = serial_ula_read(p_bbc->p_serial_ula);
    break;
  case (k_addr_master_adc + 0):
    /* Syncron reads this even on a model B. */
    if (p_bbc->is_master) {
      ret = adc_read(p_bbc->p_adc, (addr & 3));
    } else {
      /* EMU: returns 0 on an issue 3. */
      ret = 0;
      log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                           k_log_misc,
                           k_log_unimplemented,
                           "read of $FE18 region");
    }
    break;
  case 0xFE1C:
    /* EMU: a hole here. Returns 0 on an issue 3. */
    ret = 0;
    log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                         k_log_misc,
                         k_log_unimplemented,
                         "read of $FE1C region");
    break;
  case (k_addr_video_ula + 0):
    /* EMU NOTE: ULA is write-only, and reads don't seem to be wired up.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=17509
     * Break out to default 0xFE return.
     */
    (void) timing_advance_time_delta(p_timing, 1);
    break;
  case (k_addr_master_floppy + 0): /* k_addr_video_ula + 4 */
  case (k_addr_master_floppy + 4):
    if (p_bbc->is_master) {
      if (addr & 0x4) {
        /* TODO: work out if this is readable on Master or not. */
        util_bail("FDC CR read");
        break;
      }
      ret = wd_fdc_read(p_bbc->p_wd_fdc, ((addr - 0x4) & 0x7));
    } else {
      (void) timing_advance_time_delta(p_timing, 1);
    }
    break;
  case (k_addr_video_ula + 12):
    if (!p_bbc->is_master) {
      (void) timing_advance_time_delta(p_timing, 1);
    }
    break;
  case (k_addr_rom_select + 0):
    /* ROMSEL is readable on a Master but not on a model B. */
    if (p_bbc->is_master) {
      ret = p_bbc->romsel;
    }
    break;
  case (k_addr_rom_select + 4):
    if (p_bbc->is_master) {
      ret = p_bbc->acccon;
    }
    break;
  case (k_addr_rom_select + 8):
  case (k_addr_rom_select + 12):
    break;
  case (k_addr_sysvia + 0):
  case (k_addr_sysvia + 4):
  case (k_addr_sysvia + 8):
  case (k_addr_sysvia + 12):
  case (k_addr_sysvia + 16):
  case (k_addr_sysvia + 20):
  case (k_addr_sysvia + 24):
  case (k_addr_sysvia + 28):
    (void) timing_advance_time_delta(p_timing, ((cycles & 1) + 1));
    if (do_last_tick_callback) {
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    ret = via_read(p_bbc->p_system_via, (addr & 0xf));
    (void) timing_advance_time_delta(p_timing, 1);
    break;
  case (k_addr_uservia + 0):
  case (k_addr_uservia + 4):
  case (k_addr_uservia + 8):
  case (k_addr_uservia + 12):
  case (k_addr_uservia + 16):
  case (k_addr_uservia + 20):
  case (k_addr_uservia + 24):
  case (k_addr_uservia + 28):
    (void) timing_advance_time_delta(p_timing, ((cycles & 1) + 1));
    if (do_last_tick_callback) {
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    ret = via_read(p_bbc->p_user_via, (addr & 0xf));
    (void) timing_advance_time_delta(p_timing, 1);
    break;
  case (k_addr_floppy + 0):
  case (k_addr_floppy + 4):
  case (k_addr_floppy + 8):
  case (k_addr_floppy + 12):
  case (k_addr_floppy + 16):
  case (k_addr_floppy + 20):
  case (k_addr_floppy + 24):
  case (k_addr_floppy + 28):
    if (!p_bbc->is_master) {
      if (p_bbc->is_wd_fdc) {
        ret = wd_fdc_read(p_bbc->p_wd_fdc, (addr & 0x7));
      } else {
        ret = intel_fdc_read(p_bbc->p_intel_fdc, (addr & 0x7));
      }
    }
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
    if (!p_bbc->is_master) {
      ret = adc_read(p_bbc->p_adc, (addr & 3));
    } else {
      log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                           k_log_misc,
                           k_log_unimplemented,
                           "read of $FEC0-$FEDF region");
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
        ret = (timing_get_total_timer_ticks(p_timing) -
               p_bbc->cycle_count_baseline);
      default:
        break;
      }
    }
    /* Not present. */
    break;
  default:
    assert(addr >= (k_bbc_os_rom_offset - 0x100));
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
      ret = p_mem_read[addr];
    } else if (addr >= k_addr_shiela) {
      /* We should have every address covered above. */
      assert(0);
    } else {
      /* EMU: This value, as well as the 0xFE default, copied from b-em /
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

uint8_t
bbc_get_acccon(struct bbc_struct* p_bbc) {
  return p_bbc->acccon;
}

static uint8_t
bbc_get_effective_bank(struct bbc_struct* p_bbc, uint8_t romsel) {
  romsel &= 0xF;

  if (!p_bbc->is_extended_rom_addressing) {
    assert(!p_bbc->is_master);
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

static void
bbc_page_rom(struct bbc_struct* p_bbc,
             uint8_t effective_curr_bank,
             uint8_t effective_new_bank,
             uint8_t* p_sideways_old,
             uint8_t* p_sideways_new) {
  size_t map_size;
  size_t half_map_size;
  size_t map_offset;

  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  int curr_is_ram = p_bbc->is_sideways_ram_bank[effective_curr_bank];
  int new_is_ram = p_bbc->is_sideways_ram_bank[effective_new_bank];
  uint8_t* p_mem_sideways = (p_bbc->p_mem_raw + k_bbc_sideways_offset);
  intptr_t mem_handle = p_bbc->mem_handle;

  /* If current bank is RAM, save it. */
  if (curr_is_ram) {
    (void) memcpy(p_sideways_old, p_mem_sideways, k_bbc_rom_size);
  }

  (void) memcpy(p_mem_sideways, p_sideways_new, k_bbc_rom_size);

  /* The BBC Master mode does not support JIT (so no invalidate required).
   * The BBC Master has all sorts of pageable regions, and the virtual memory
   * tricks possible with the model B's clean RAM / sideways / OS ROM split
   * are not possible.
   */
  if (p_bbc->is_master) {
    return;
  }

  p_cpu_driver->p_funcs->memory_range_invalidate(p_cpu_driver,
                                                 k_bbc_sideways_offset,
                                                 k_bbc_rom_size);

  if (curr_is_ram == new_is_ram) {
    return;
  }

  /* We flipped from ROM to RAM or visa versa, we need to update the write
   * mapping with either a dummy area (ROM) or the real sideways area (RAM).
   */
  map_size = (k_6502_addr_space_size * 2);
  half_map_size = (map_size / 2);
  map_offset = (k_6502_addr_space_size / 2);

  os_alloc_free_mapping(p_bbc->p_mapping_write_2);

  if (new_is_ram) {
    p_bbc->p_mapping_write_2 = os_alloc_get_mapping_from_handle(
        mem_handle,
        (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR + map_offset),
        half_map_size,
        half_map_size);
    os_alloc_make_mapping_none((p_bbc->p_mem_write + k_bbc_os_rom_offset),
                               k_bbc_rom_size);
  } else {
    p_bbc->p_mapping_write_2 = os_alloc_get_mapping(
        (void*) (size_t) (K_BBC_MEM_WRITE_FULL_ADDR + map_offset),
        half_map_size);
  }
  os_alloc_make_mapping_none((p_bbc->p_mem_write + k_6502_addr_space_size),
                             map_offset);

  if (p_bbc->p_mapping_write_ind_2 == NULL) {
    return;
  }

  os_alloc_free_mapping(p_bbc->p_mapping_write_ind_2);

  if (new_is_ram) {
    p_bbc->p_mapping_write_ind_2 = os_alloc_get_mapping_from_handle(
        mem_handle,
        (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR + map_offset),
        half_map_size,
        half_map_size);
    os_alloc_make_mapping_none((p_bbc->p_mem_write_ind + k_bbc_os_rom_offset),
                               k_bbc_rom_size);
  } else {
    p_bbc->p_mapping_write_ind_2 = os_alloc_get_mapping(
        (void*) (size_t) (K_BBC_MEM_WRITE_IND_ADDR + map_offset),
        half_map_size);
    os_alloc_make_mapping_none(
        (p_bbc->p_mem_write_ind + K_BBC_MEM_INACCESSIBLE_OFFSET),
        K_BBC_MEM_INACCESSIBLE_LEN);
  }

  os_alloc_make_mapping_none(
      (p_bbc->p_mem_write_ind + k_6502_addr_space_size),
      map_offset);
}

void
bbc_sideways_select(struct bbc_struct* p_bbc, uint8_t val) {
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
  int is_sideways_slot_changing;

  uint8_t* p_mem_sideways = (p_bbc->p_mem_raw + k_bbc_sideways_offset);
  uint8_t* p_sideways_old = p_bbc->p_mem_sideways;
  uint8_t* p_sideways_new = p_bbc->p_mem_sideways;
  uint8_t curr_romsel = p_bbc->romsel;
  int is_curr_andy = 0;
  int is_new_andy = 0;

  /* TODO: mask so it reads back correctly on Master. */
  p_bbc->romsel = val;

  effective_curr_bank = bbc_get_effective_bank(p_bbc, curr_romsel);
  effective_new_bank = bbc_get_effective_bank(p_bbc, val);
  assert(!(effective_curr_bank & 0xF0));
  assert(!(effective_new_bank & 0xF0));

  is_sideways_slot_changing = 0;
  if ((effective_new_bank != effective_curr_bank) ||
      p_bbc->is_romsel_invalidated) {
    is_sideways_slot_changing = 1;
  }

  p_sideways_new += (effective_new_bank * k_bbc_rom_size);
  p_sideways_old += (effective_curr_bank * k_bbc_rom_size);

  if (p_bbc->is_master) {
    is_curr_andy = (curr_romsel & k_romsel_andy);
    is_new_andy = (val & k_romsel_andy);
    if (is_curr_andy) {
      if (!is_new_andy || is_sideways_slot_changing) {
        /* Save ANDY back to its store. */
        (void) memcpy(p_bbc->p_mem_andy, p_mem_sideways, k_bbc_andy_size);
        /* Restore what is underneath ANDY. */
        (void) memcpy(p_mem_sideways, p_sideways_old, k_bbc_andy_size);
      }
    }
  }

  if (is_sideways_slot_changing) {
    bbc_page_rom(p_bbc,
                 effective_curr_bank,
                 effective_new_bank,
                 p_sideways_old,
                 p_sideways_new);
  }

  p_bbc->is_romsel_invalidated = 0;

  if (p_bbc->is_master) {
    if (is_new_andy) {
      if (!is_curr_andy || is_sideways_slot_changing) {
        /* Save data underneath ANDY in case it is RAM. */
        (void) memcpy(p_sideways_new, p_mem_sideways, k_bbc_andy_size);
        /* Copy in ANDY memory. */
        (void) memcpy(p_mem_sideways, p_bbc->p_mem_andy, k_bbc_andy_size);
      }
    }
  }
}

static int
bbc_set_acccon(struct bbc_struct* p_bbc, uint8_t new_acccon) {
  int mos_access_shadow;
  uint8_t curr_acccon = p_bbc->acccon;
  int is_curr_display_lynne = !!(curr_acccon & k_acccon_display_lynne);
  int is_curr_lynne = !!(curr_acccon & k_acccon_lynne);
  int is_curr_hazel = !!(curr_acccon & k_acccon_hazel);
  int is_new_display_lynne = !!(new_acccon & k_acccon_display_lynne);
  int is_new_access_lynne_from_os =
      !!(new_acccon & k_acccon_access_lynne_from_os);
  int is_new_lynne = !!(new_acccon & k_acccon_lynne);
  int is_new_hazel = !!(new_acccon & k_acccon_hazel);

  assert(p_bbc->is_master);

  /* The video subsystem needs to know if it is displaying shadow RAM or not. */
  /* This needs to happen before we page the RAM around, so that the video
   * rendering can catch up with the current setup.
   */
  if ((is_new_display_lynne != is_curr_display_lynne) ||
      (is_new_lynne != is_curr_lynne)) {
    /* We currently do copying, not paging, of shadow RAM, thanks to Windows
     * paging limitations.
     * This means we need to display "shadow" RAM in non-shadow mode, if the
     * shadow RAM is paged in. This is because in that case, the normal RAM
     * for normal mode will have been copied / swapped with the shadow RAM.
     */
    int is_shadow_display = (is_new_display_lynne ^ is_new_lynne);
    video_shadow_mode_updated(p_bbc->p_video, is_shadow_display);
  }

  if (is_curr_lynne ^ is_new_lynne) {
    size_t val;
    uint32_t i;
    uint32_t count = (k_bbc_lynne_size / sizeof(val));
    size_t* p1 = (size_t*) p_bbc->p_mem_lynne;
    size_t* p2 = (size_t*) (p_bbc->p_mem_raw + k_bbc_shadow_offset);
    for (i = 0; i < count; ++i) {
      val = p1[i];
      p1[i] = p2[i];
      p2[i] = val;
    }
  }

  p_bbc->acccon = new_acccon;

  /* HAZEL paging. */
  if (is_curr_hazel ^ is_new_hazel) {
    uint8_t* p_raw_mem_hazel = (p_bbc->p_mem_raw + k_bbc_os_rom_offset);
    if (is_new_hazel) {
      (void) memcpy(p_raw_mem_hazel, p_bbc->p_mem_hazel, k_bbc_hazel_size);
    } else {
      (void) memcpy(p_bbc->p_mem_hazel, p_raw_mem_hazel, k_bbc_hazel_size);
      (void) memcpy(p_raw_mem_hazel, p_bbc->p_os_rom, k_bbc_hazel_size);
    }
  }

  /* Trap access to 0x3000 - 0x7FFF if the crazy MOS ROM VDU access is different
   * to normal access.
   */
  /* Reference: b2 source code. Thanks Tom Seddon.
   * Also, see test/games/Nubium 20181214 b.ssd, which doesn't run correctly
   * on a correctly emulated Master, see:
   * https://stardot.org.uk/forums/viewtopic.php?p=223299#p223299
   * Note that the combination that surprises me is HAZEL=0, LYNNE=1, E=0.
   * This causes the MOS VDU code region to access _main_ RAM, not LYNNE as
   * you might expect.
   */
  mos_access_shadow = ((is_new_hazel && is_new_lynne) ||
                       (!is_new_hazel && is_new_access_lynne_from_os));
  if (mos_access_shadow != is_new_lynne) {
    if (!p_bbc->is_acccon_usr_mos_different) {
      log_do_log_max_count(&p_bbc->log_count_shadow_speed,
                           k_log_misc,
                           k_log_info,
                           "shadow region SLOW access");
    }
    p_bbc->is_acccon_usr_mos_different = 1;
    p_bbc->write_callback_from = k_bbc_shadow_offset;
    p_bbc->read_callback_from = k_bbc_shadow_offset;
  } else {
    if (p_bbc->is_acccon_usr_mos_different) {
      log_do_log_max_count(&p_bbc->log_count_shadow_speed,
                           k_log_misc,
                           k_log_info,
                           "shadow region fast access");
    }
    p_bbc->is_acccon_usr_mos_different = 0;
    p_bbc->write_callback_from = k_bbc_sideways_offset;
    p_bbc->read_callback_from = 0xFC00;
  }

  /* Always force reload of write callback address. */
  return 1;
}

static void
bbc_test_nmi_timer_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  (void) timing_stop_timer(p_bbc->p_timing, p_bbc->timer_id_test_nmi);

  if (p_bbc->p_intel_fdc != NULL) {
    intel_fdc_testing_fire_nmi(p_bbc->p_intel_fdc);
  }
}

int
bbc_write_callback(void* p,
                   uint16_t addr,
                   uint8_t val,
                   uint16_t pc,
                   int do_last_tick_callback) {
  uint64_t cycles;
  int ret = 0;
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct timing_struct* p_timing = p_bbc->p_timing;

  if (p_bbc->is_compat_old_1MHz_cycles) {
    cycles = state_6502_get_cycles(p_bbc->p_state_6502);
  } else {
    cycles = timing_get_total_timer_ticks(p_timing);
  }

  bbc_do_pre_read_write_tick_handling(p_bbc,
                                      addr,
                                      cycles,
                                      do_last_tick_callback);

  if (p_bbc->is_master && (addr < k_addr_fred)) {
    bbc_do_master_ram_write(p_bbc, addr, val, pc);
    return 0;
  }

  p_bbc->num_hw_reg_hits++;

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
    mc6850_write(p_bbc->p_serial, (addr & 0x1), val);
    break;
  case (k_addr_serial_ula + 0):
  case (k_addr_serial_ula + 4):
    serial_ula_write(p_bbc->p_serial_ula, val);
    break;
  case (k_addr_master_adc + 0):
    if (p_bbc->is_master) {
      adc_write(p_bbc->p_adc, (addr & 3), val);
    } else {
      log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                           k_log_misc,
                           k_log_unimplemented,
                           "write of $FE18 region");
    }
    break;
  case (k_addr_master_adc + 4):
    log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                         k_log_misc,
                         k_log_unimplemented,
                         "write of $FE1C region");
    break;
  case (k_addr_video_ula + 0):
    {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      video_ula_write(p_video, (addr & 0x1), val);
      (void) timing_advance_time_delta(p_timing, 1);
    }
    break;
  case (k_addr_master_floppy + 0): /* k_addr_video_ula + 4 */
  case (k_addr_master_floppy + 4):
    if (p_bbc->is_master) {
      wd_fdc_write(p_bbc->p_wd_fdc, ((addr - 0x4) & 0x7), val);
    } else {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      video_ula_write(p_video, (addr & 0x1), val);
      (void) timing_advance_time_delta(p_timing, 1);
    }
    break;
  case (k_addr_video_ula + 12):
    if (!p_bbc->is_master) {
      struct video_struct* p_video = bbc_get_video(p_bbc);
      video_ula_write(p_video, (addr & 0x1), val);
      (void) timing_advance_time_delta(p_timing, 1);
    }
    break;
  case (k_addr_rom_select + 0):
    bbc_sideways_select(p_bbc, val);
    break;
  case (k_addr_rom_select + 4):
    if (p_bbc->is_master) {
      ret = bbc_set_acccon(p_bbc, val);
    } else {
      bbc_sideways_select(p_bbc, val);
    }
    break;
  case (k_addr_rom_select + 8):
  case (k_addr_rom_select + 12):
    if (!p_bbc->is_master) {
      bbc_sideways_select(p_bbc, val);
    }
    break;
  case (k_addr_sysvia + 0):
  case (k_addr_sysvia + 4):
  case (k_addr_sysvia + 8):
  case (k_addr_sysvia + 12):
  case (k_addr_sysvia + 16):
  case (k_addr_sysvia + 20):
  case (k_addr_sysvia + 24):
  case (k_addr_sysvia + 28):
    (void) timing_advance_time_delta(p_timing, ((cycles & 1) + 1));
    if (do_last_tick_callback) {
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    via_write(p_bbc->p_system_via, (addr & 0xf), val);
    (void) timing_advance_time_delta(p_timing, 1);
    break;
  case (k_addr_uservia + 0):
  case (k_addr_uservia + 4):
  case (k_addr_uservia + 8):
  case (k_addr_uservia + 12):
  case (k_addr_uservia + 16):
  case (k_addr_uservia + 20):
  case (k_addr_uservia + 24):
  case (k_addr_uservia + 28):
    (void) timing_advance_time_delta(p_timing, ((cycles & 1) + 1));
    if (do_last_tick_callback) {
      p_bbc->memory_access.memory_client_last_tick_callback(
          p_bbc->memory_access.p_last_tick_callback_obj);
    }
    via_write(p_bbc->p_user_via, (addr & 0xf), val);
    (void) timing_advance_time_delta(p_timing, 1);
    break;
  case (k_addr_floppy + 0):
  case (k_addr_floppy + 4):
  case (k_addr_floppy + 8):
  case (k_addr_floppy + 12):
  case (k_addr_floppy + 16):
  case (k_addr_floppy + 20):
  case (k_addr_floppy + 24):
  case (k_addr_floppy + 28):
    if (!p_bbc->is_master) {
      if (p_bbc->is_wd_fdc) {
        wd_fdc_write(p_bbc->p_wd_fdc, (addr & 0x7), val);
      } else {
        intel_fdc_write(p_bbc->p_intel_fdc, (addr & 0x7), val);
      }
    }
    break;
  case (k_addr_econet + 0):
  case (k_addr_econet + 4):
  case (k_addr_econet + 8):
  case (k_addr_econet + 12):
  case (k_addr_econet + 16):
  case (k_addr_econet + 20):
  case (k_addr_econet + 24):
  case (k_addr_econet + 28):
    log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                         k_log_misc,
                         k_log_unimplemented,
                         "write of ECONET region");
    break;
  case (k_addr_adc + 0):
  case (k_addr_adc + 4):
  case (k_addr_adc + 8):
  case (k_addr_adc + 12):
  case (k_addr_adc + 16):
  case (k_addr_adc + 20):
  case (k_addr_adc + 24):
  case (k_addr_adc + 28):
    if (!p_bbc->is_master) {
      adc_write(p_bbc->p_adc, (addr & 3), val);
    } else {
      log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                           k_log_misc,
                           k_log_unimplemented,
                           "write of $FEC0-$FEDF region");
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
      case k_addr_tube:
        /* &FEE0: caush crash. */
        *((volatile uint8_t*) 0xdead) = '\x41';
        break;
      case (k_addr_tube + 1):
        /* &FEE1: reset cycles count. */
        p_bbc->cycle_count_baseline = timing_get_total_timer_ticks(p_timing);
        break;
      case (k_addr_tube + 2):
        /* &FEE2: exit. */
        if (val != 0xA5) {
          *((volatile uint8_t*) 0xdead) = '\x41';
        }
        p_bbc->p_cpu_driver->p_funcs->apply_flags(p_bbc->p_cpu_driver,
                                                  k_cpu_flag_exited,
                                                  0);
        p_bbc->p_cpu_driver->p_funcs->set_exit_value(p_bbc->p_cpu_driver,
                                                     0x434241);
        break;
      case (k_addr_tube + 3):
        /* &FEE3: raise NMI. */
        state_6502_set_irq_level(p_bbc->p_state_6502, k_state_6502_irq_nmi, 0);
        state_6502_set_irq_level(p_bbc->p_state_6502, k_state_6502_irq_nmi, 1);
        break;
      case (k_addr_tube + 4):
        /* &FEE4: raise 8271 NMI after a certain cycle count. */
        if (p_bbc->timer_id_test_nmi == -1) {
          p_bbc->timer_id_test_nmi =
              timing_register_timer(p_bbc->p_timing,
                                    bbc_test_nmi_timer_callback,
                                    p_bbc);
        }
        if (!timing_timer_is_running(p_timing, p_bbc->timer_id_test_nmi)) {
          (void) timing_start_timer_with_value(p_timing,
                                               p_bbc->timer_id_test_nmi,
                                               val);
        }
        break;
      default:
        break;
      }
    } else {
      log_do_log_max_count(&p_bbc->log_count_misc_unimplemented,
                           k_log_misc,
                           k_log_unimplemented,
                           "write of TUBE region");
    }
    break;
  default:
    assert(addr >= (k_bbc_os_rom_offset - 0x100));
    if ((addr < k_bbc_registers_start) ||
        (addr >= (k_bbc_registers_start + k_bbc_registers_len))) {
      /* If we miss the registers, it will be:
       * 1) $FF00 - $FFFF on account of trapping on anything above $FC00, or
       * 2) $FBxx on account of the X uncertainty of $FBxx,X, or
       * 3) The Windows port needs a wider range to trap ROM writes.
       */
    } else if (addr >= k_addr_shiela) {
      /* We should have every address covered above. */
      assert(0);
    }
    break;
  }

  return ret;
}

static void
bbc_jit_encoding_handle_timing(struct bbc_struct* p_bbc,
                               struct asm_uop** p_p_uop,
                               int* p_ends_block,
                               uint32_t* p_extra_cycles,
                               uint16_t addr_6502,
                               int do_accurate_timings) {
  struct asm_uop* p_uop = *p_p_uop;
  int is_1MHz = bbc_is_1MHz_address(p_bbc, addr_6502);

  *p_ends_block = 0;
  if (do_accurate_timings) {
    /* A subtlety: JIT fires countdown when it hits -1, not when it hits 0.
     * For tick-then-read or tick-then-write hardware register access, an
     * event that might affect a result might go missing if it occurs at the
     * countdown==0 boundary.
     * To compensate, claim the instruction takes a cycle longer but then
     * fix up.
     */
    *p_extra_cycles = 1;
    asm_make_uop1(p_uop, k_opcode_add_cycles, 1);
    p_uop++;
  } else {
    *p_extra_cycles = 0;
  }

  if (is_1MHz) {
    if (do_accurate_timings) {
      *p_ends_block = 1;
      *p_extra_cycles += 2;
      asm_make_uop1(p_uop, k_opcode_deref_scratch, 0x0);
      p_uop++;
      asm_make_uop1(p_uop, k_opcode_load_deref_scratch_quad, 0x10);
      p_uop++;
      asm_make_uop0(p_uop, k_opcode_sync_even_cycle);
      p_uop++;
    } else {
      *p_extra_cycles += 1;
    }
  }

  *p_p_uop = p_uop;
}

uint32_t
bbc_get_read_jit_encoding(void* p,
                          struct asm_uop* p_uops,
                          int* p_ends_block,
                          uint32_t* p_extra_cycles,
                          uint32_t num_uops,
                          uint16_t addr_6502,
                          int do_accurate_timings) {
  struct bbc_struct* p_bbc;
  struct asm_uop* p_uop = p_uops;
  int is_call = 0;
  uint32_t func_offset = 0;
  uint32_t param_offset = 0;
  uint32_t field_offset = 0;
  int syncs_time = 0;

  (void) num_uops;

  p_bbc = (struct bbc_struct*) p;

  switch (addr_6502) {
  case 0xFE08:
    param_offset = 0x60;
    field_offset = 0x18;
    break;
  case 0xFE45:
    is_call = 1;
    func_offset = 0x18;
    param_offset = 0x8;
    syncs_time = 1;
    break;
  case 0xFE49:
    is_call = 1;
    func_offset = 0x20;
    param_offset = 0x8;
    syncs_time = 1;
    break;
  case 0xFE4D:
    param_offset = 0x8;
    field_offset = 0x59;
    break;
  case 0xFE4E:
    param_offset = 0x8;
    field_offset = 0x5A;
    break;
  case 0xFE4F:
    is_call = 1;
    func_offset = 0x28;
    param_offset = 0x8;
    break;
  case 0xFE65:
    is_call = 1;
    func_offset = 0x18;
    param_offset = 0x10;
    syncs_time = 1;
    break;
  case 0xFE69:
  case 0xFE79: /* Castle Quest hits this alias. */
    is_call = 1;
    func_offset = 0x20;
    param_offset = 0x10;
    syncs_time = 1;
    break;
  case 0xFE6D:
    param_offset = 0x10;
    field_offset = 0x59;
    break;
  case 0xFE80:
    if (p_bbc->is_wd_fdc) {
      return 0;
    }
    param_offset = 0x68;
    field_offset = 0x68;
    break;
  case 0xFEC0:
    param_offset = 0x50;
    field_offset = 0x20;
    break;
  case 0xFEC1:
    param_offset = 0x50;
    field_offset = 0x21;
    break;
  case 0xFEC2:
    param_offset = 0x50;
    field_offset = 0x22;
    break;
  default:
    /* Bail. */
    return 0;
  }

  /* TODO: fetch these unseemly constants in a more graceful manner! */
  asm_make_uop1(p_uop, k_opcode_deref_context, 0x40078);
  p_uop++;

  if (is_call) {
    /* Save registers.
     * Do this before the 1MHz timing adjustment so that the timing adjustment
     * can trash the NZ flags in the host flags if desired.
     */
    asm_make_uop0(p_uop, k_opcode_save_regs);
    p_uop++;
  }

  bbc_jit_encoding_handle_timing(p_bbc,
                                 &p_uop,
                                 p_ends_block,
                                 p_extra_cycles,
                                 addr_6502,
                                 do_accurate_timings);

  if (is_call) {
    /* Set up param2. */
    asm_make_uop1(p_uop, k_opcode_set_param2, (addr_6502 & 0xF));
    p_uop++;

    /* Set up param4. */
    if (syncs_time) {
      asm_make_uop0(p_uop, k_opcode_set_param3_from_countdown);
      p_uop++;
    }

    /* Call C function. */
    asm_make_uop2(p_uop,
                  k_opcode_call_scratch_param,
                  param_offset,
                  func_offset);
    p_uop++;

    asm_make_uop0(p_uop, k_opcode_set_value_from_ret);
    p_uop++;

    /* Restore registers. */
    asm_make_uop0(p_uop, k_opcode_restore_regs);
    p_uop++;
  } else {
    asm_make_uop1(p_uop, k_opcode_deref_scratch, param_offset);
    p_uop++;
    asm_make_uop1(p_uop, k_opcode_load_deref_scratch, field_offset);
    p_uop++;
  }

  return (p_uop - p_uops);
}

uint32_t
bbc_get_write_jit_encoding(void* p,
                           struct asm_uop* p_uops,
                           int* p_ends_block,
                           uint32_t* p_extra_cycles,
                           uint32_t num_uops,
                           uint16_t addr_6502,
                           int do_accurate_timings) {
  struct bbc_struct* p_bbc;
  struct asm_uop* p_uop = p_uops;
  uint32_t func_offset = 0;
  uint32_t param_offset = 0;
  int syncs_time = 0;
  int returns_time = 0;

  (void) num_uops;

  p_bbc = (struct bbc_struct*) p;

  switch (addr_6502) {
  case 0xFE00:
    func_offset = 0x78;
    param_offset = 0x70;
    break;
  case 0xFE40:
    func_offset = 0x30;
    param_offset = 0x8;
    break;
  case 0xFE43:
    func_offset = 0x38;
    param_offset = 0x8;
    break;
  case 0xFE4D:
    func_offset = 0x40;
    param_offset = 0x8;
    syncs_time = 1;
    break;
  case 0xFE4F:
    func_offset = 0x48;
    param_offset = 0x8;
    break;
  case 0xFEC0:
    func_offset = 0x58;
    param_offset = 0x50;
    syncs_time = 1;
    returns_time = 1;
    break;
  default:
    /* Bail. */
    return 0;
  }

  /* TODO: fetch these unseemly constants in a more graceful manner! */
  asm_make_uop1(p_uop, k_opcode_deref_context, 0x40078);
  p_uop++;

  /* Save registers.
   * Do this before the 1MHz timing adjustment so that the timing adjustment
   * can trash the NZ flags in the host flags if desired.
   */
  asm_make_uop0(p_uop, k_opcode_save_regs);
  p_uop++;

  /* Set up param3.
   * Do it before the timing fixup because that will trash the value in
   * the register.
   */
  asm_make_uop0(p_uop, k_opcode_set_param3_from_value);
  p_uop++;

  bbc_jit_encoding_handle_timing(p_bbc,
                                 &p_uop,
                                 p_ends_block,
                                 p_extra_cycles,
                                 addr_6502,
                                 do_accurate_timings);

  /* Set up param2. */
  asm_make_uop1(p_uop, k_opcode_set_param2, (addr_6502 & 0xF));
  p_uop++;

  /* Set up param4. */
  if (syncs_time) {
    asm_make_uop0(p_uop, k_opcode_set_param4_from_countdown);
    p_uop++;
  }

  /* Call C function. */
  asm_make_uop2(p_uop, k_opcode_call_scratch_param, param_offset, func_offset);
  p_uop++;

  if (returns_time) {
    asm_make_uop0(p_uop, k_opcode_set_countdown_from_ret);
    p_uop++;
  }

  /* Restore registers. */
  asm_make_uop0(p_uop, k_opcode_restore_regs);
  p_uop++;

  return (p_uop - p_uops);
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
                               int framing_changed,
                               int do_wait_for_paint) {
  struct bbc_message message;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  if (!p_bbc->fast_flag) {
    do_wait_for_paint = 1;
  }

  message.data[0] = k_message_vsync;
  message.data[1] = do_full_render;
  message.data[2] = framing_changed;
  message.data[3] = timing_get_total_timer_ticks(p_bbc->p_timing);
  message.data[4] = do_wait_for_paint;
  bbc_cpu_send_message(p_bbc, &message);
  if (do_wait_for_paint) {
    struct bbc_message message;
    bbc_cpu_receive_message(p_bbc, &message);
    assert(message.data[0] == k_message_render_done);
  }
}

static void
bbc_break_reset(struct bbc_struct* p_bbc) {
  uint64_t ticks = timing_get_total_timer_ticks(p_bbc->p_timing);
  log_do_log(k_log_misc, k_log_info, "BREAK reset at ticks %"PRIu64, ticks);

  /* The BBC break key is attached to the 6502 reset line.
   * Many other peripherals are not connected to any reset on break, but a few
   * are.
   */
  if (p_bbc->is_wd_fdc) {
    wd_fdc_break_reset(p_bbc->p_wd_fdc);
  } else {
    intel_fdc_break_reset(p_bbc->p_intel_fdc);
  }
  state_6502_reset(p_bbc->p_state_6502);

  if (p_bbc->is_compat_old_1MHz_cycles) {
    timing_set_odd_even_mixin(p_bbc->p_timing, (ticks & 1));
  }
}

static void
bbc_virtual_keyboard_updated_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  /* Make sure interrupt state is synced with new keyboard state. */
  (void) via_update_port_a(p_bbc->p_system_via);

  /* Check for BREAK key. */
  if (keyboard_consume_key_press(p_bbc->p_keyboard, k_keyboard_key_f12) ||
      keyboard_consume_key_press(p_bbc->p_keyboard, k_keyboard_key_delete)) {
    /* We're in the middle of some timer callback. Let the CPU driver initiate
     * the actual reset at a safe time.
     */
    struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
    p_cpu_driver->p_funcs->apply_flags(p_cpu_driver, k_cpu_flag_soft_reset, 0);
  }
}

static void
bbc_do_reset_callback(void* p, uint32_t flags) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  if (flags & k_cpu_flag_soft_reset) {
    bbc_break_reset(p_bbc);
  }
  if (flags & k_cpu_flag_hard_reset) {
    bbc_power_on_reset(p_bbc);
  }
  if (flags & k_cpu_flag_replay) {
    keyboard_rewind(p_bbc->p_keyboard, p_bbc->rewind_to_cycles);
  }

  p_cpu_driver->p_funcs->apply_flags(
      p_cpu_driver,
      0,
      (k_cpu_flag_soft_reset | k_cpu_flag_hard_reset | k_cpu_flag_replay));
}

static void
bbc_set_fast_mode_callback(void* p, int is_fast) {
  struct cpu_driver* p_cpu_driver;
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  void (*p_memory_written_callback)(void* p) = NULL;

  p_bbc->fast_flag = is_fast;

  /* In accurate mode, and when not running super fast, we use the interpreter
   * with a special callback to sync 6502 memory writes to the 6845 CRTC memory
   * reads.
   */
  if (!p_bbc->options.accurate) {
    return;
  }
  if (!p_bbc->do_video_memory_sync) {
    return;
  }
  p_cpu_driver = p_bbc->p_cpu_driver;
  if (!is_fast) {
    p_memory_written_callback = video_advance_for_memory_sync;
  }
  p_cpu_driver->p_funcs->set_memory_written_callback(p_cpu_driver,
                                                     p_memory_written_callback,
                                                     p_bbc->p_video);
}

int
bbc_get_fast_flag(struct bbc_struct* p_bbc) {
  return p_bbc->fast_flag;
}

void
bbc_set_fast_flag(struct bbc_struct* p_bbc, int is_fast) {
  bbc_set_fast_mode_callback((void*) p_bbc, is_fast);
}

void
bbc_set_compat_old_1MHz_cycles(struct bbc_struct* p_bbc) {
  p_bbc->is_compat_old_1MHz_cycles = 1;
}

static void
bbc_reset_callback_baselines(struct bbc_struct* p_bbc) {
  /* Selects 0xFC00 - 0xFFFF which is broader than the needed 0xFC00 - 0xFEFF
   * for hardware registers, but that's fine.
   */
  p_bbc->read_callback_from = 0xFC00;

  /* TODO: we can do better (less callbacking). */
  if (p_bbc->is_master) {
    p_bbc->write_callback_from = k_bbc_sideways_offset;
  } else {
    p_bbc->write_callback_from = k_bbc_os_rom_offset;
  }
}

static void
bbc_setup_indirect_mappings(struct bbc_struct* p_bbc,
                            size_t map_size,
                            size_t half_map_size,
                            size_t map_offset) {
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
  /* Make the ROM read-only. */
  os_alloc_make_mapping_read_only((p_bbc->p_mem_read_ind + k_bbc_ram_size),
                                  (k_6502_addr_space_size - k_bbc_ram_size));

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
}

static void
bbc_CA2_changed_callback(void* p, int level, int output) {
  struct bbc_struct* p_bbc;
  struct via_struct* p_user_via;
  uint8_t val;

  if (level || !output) {
    return;
  }

  p_bbc = (struct bbc_struct*) p;

  if (p_bbc->p_printer_file == NULL) {
    p_bbc->p_printer_file = util_file_try_open("beebjit.printer", 1, 1);
    if (p_bbc->p_printer_file == NULL) {
      log_do_log(k_log_misc, k_log_error, "FAILED to create printer file");
    } else {
      log_do_log(k_log_misc, k_log_info, "created printer file");
    }
  }

  p_user_via = p_bbc->p_user_via;
  val = via_calculate_port_a(p_user_via);

  if (p_bbc->p_printer_file != NULL) {
    /* Replace CR with LF. */
    if (val == 0x0D) {
      val = 0x0A;
    }
    util_file_write(p_bbc->p_printer_file, &val, 1);
  }

  /* This is the world's fastest printer. It acks the character simultaneously
   * with it being transmitted!
   */
  via_set_CA1(p_user_via, 0);
  via_set_CA1(p_user_via, 1);
}

struct bbc_struct*
bbc_create(int mode,
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
           const char* p_log_flags) {
  struct timing_struct* p_timing;
  struct state_6502* p_state_6502;
  struct debug_struct* p_debug;
  uint32_t cpu_scale_factor;
  size_t map_size;
  size_t half_map_size;
  size_t map_offset;
  uint8_t* p_mem_raw;
  uint8_t* p_os_start;

  int externally_clocked_via = 1;
  int externally_clocked_crtc = 1;
  int externally_clocked_adc = 1;
  int synchronous_sound = 0;
  int is_65c12 = is_master;

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
  p_bbc->is_master = is_master;
  p_bbc->p_os_rom = p_os_rom;
  p_bbc->is_extended_rom_addressing = is_master;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;
  p_bbc->fast_flag = fast_flag;
  p_bbc->test_map_flag = test_map_flag;
  p_bbc->is_wd_fdc = (wd_1770_type > 0);
  p_bbc->is_wd_1772 = (wd_1770_type == 2);
  p_bbc->exit_value = 0;
  p_bbc->handle_channel_read_bbc = -1;
  p_bbc->handle_channel_write_bbc = -1;
  p_bbc->handle_channel_read_client = -1;
  p_bbc->handle_channel_write_client = -1;
  p_bbc->timer_id_autoboot = -1;
  p_bbc->timer_id_test_nmi = -1;

  p_bbc->do_video_memory_sync = 1;
  if (util_has_option(p_opt_flags, "video:no-memory-sync")) {
    p_bbc->do_video_memory_sync = 0;
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
  p_bbc->log_timestamp = util_has_option(p_log_flags, "perf:timestamp");

  bbc_reset_callback_baselines(p_bbc);

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
  p_bbc->log_count_shadow_speed = 16;
  p_bbc->log_count_misc_unimplemented = 32;

  p_bbc->p_mapping_raw =
      os_alloc_get_mapping_from_handle(
          p_bbc->mem_handle,
          (void*) (size_t) (K_BBC_MEM_RAW_ADDR - map_offset),
          0,
          map_size);
  p_mem_raw = (os_alloc_get_mapping_addr(p_bbc->p_mapping_raw) + map_offset);
  p_bbc->p_mem_raw = p_mem_raw;
  os_alloc_make_mapping_none((p_mem_raw - map_offset), map_offset);
  os_alloc_make_mapping_none((p_mem_raw + k_6502_addr_space_size), map_offset);

  /* Copy in the OS ROM. */
  p_os_start = (p_mem_raw + k_bbc_os_rom_offset);
  (void) memcpy(p_os_start, p_bbc->p_os_rom, k_bbc_rom_size);

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
  if (asm_jit_uses_indirect_mappings()) {
    bbc_setup_indirect_mappings(p_bbc, map_size, half_map_size, map_offset);
  }

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
  /* TODO: we can widen what we make read-only? */
  /* Make the ROM readonly in the read mapping used at runtime. */
  os_alloc_make_mapping_read_only((p_bbc->p_mem_read + k_bbc_ram_size),
                                  (k_6502_addr_space_size - k_bbc_ram_size));

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

  /* Special memory chunks on a Master. */
  if (p_bbc->is_master) {
    p_bbc->p_mem_master = util_mallocz(
        k_bbc_andy_size + k_bbc_hazel_size + k_bbc_lynne_size);
    p_bbc->p_mem_andy = p_bbc->p_mem_master;
    p_bbc->p_mem_hazel = (p_bbc->p_mem_andy + k_bbc_andy_size);
    p_bbc->p_mem_lynne = (p_bbc->p_mem_hazel + k_bbc_hazel_size);
  }

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
  p_bbc->memory_access.memory_get_read_jit_encoding = bbc_get_read_jit_encoding;
  p_bbc->memory_access.memory_get_write_jit_encoding =
      bbc_get_write_jit_encoding;

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
    externally_clocked_adc = 0;
    synchronous_sound = 1;
  }

  p_timing = timing_create(cpu_scale_factor);
  p_bbc->p_timing = p_timing;

  p_state_6502 = state_6502_create(p_timing, p_bbc->p_mem_read);
  p_bbc->p_state_6502 = p_state_6502;

  if (is_master) {
    p_bbc->p_cmos = cmos_create(&p_bbc->options);
  }

  p_bbc->p_system_via = via_create(k_via_system,
                                   externally_clocked_via,
                                   p_timing,
                                   p_bbc);
  p_bbc->p_user_via = via_create(k_via_user,
                                 externally_clocked_via,
                                 p_timing,
                                 p_bbc);
  p_bbc->p_via_read_T1CH_func = via_read_T1CH_with_countdown;
  p_bbc->p_via_read_T2CH_func = via_read_T2CH_with_countdown;
  p_bbc->p_via_read_ORAnh_func = via_read_ORAnh;
  p_bbc->p_via_write_ORB_func = via_write_ORB_with_countdown;
  p_bbc->p_via_write_DDRA_func = via_write_DDRA_with_countdown;
  p_bbc->p_via_write_IFR_func = via_write_IFR_with_countdown;
  p_bbc->p_via_write_ORAnh_func = via_write_ORAnh_with_countdown;

  p_bbc->p_keyboard = keyboard_create(p_timing, &p_bbc->options);
  keyboard_set_virtual_updated_callback(p_bbc->p_keyboard,
                                        bbc_virtual_keyboard_updated_callback,
                                        p_bbc);
  keyboard_set_fast_mode_callback(p_bbc->p_keyboard,
                                  bbc_set_fast_mode_callback,
                                  (void*) p_bbc);

  p_bbc->p_adc = adc_create(externally_clocked_adc,
                            p_timing,
                            p_bbc->p_system_via);
  p_bbc->p_adc_write_func = adc_write_with_countdown;

  p_bbc->p_joystick = joystick_create(p_bbc->p_system_via,
                                      p_bbc->p_adc,
                                      p_bbc->p_keyboard);
  if (util_has_option(p_opt_flags, "bbc:joystick-keyboard")) {
    joystick_set_use_keyboard(p_bbc->p_joystick, 1);
  }

  p_bbc->p_sound = sound_create(synchronous_sound, p_timing, &p_bbc->options);

  p_bbc->p_teletext = teletext_create();
  p_bbc->p_render = render_create(p_bbc->p_teletext, &p_bbc->options);
  p_bbc->p_video = video_create(p_bbc->p_mem_read,
                                p_bbc->p_mem_master,
                                externally_clocked_crtc,
                                p_timing,
                                p_bbc->p_render,
                                p_bbc->p_teletext,
                                p_bbc->p_system_via,
                                bbc_framebuffer_ready_callback,
                                p_bbc,
                                &p_bbc->fast_flag,
                                &p_bbc->options);
  p_bbc->p_video_write_func = video_crtc_write;

  p_bbc->p_drive_0 = disc_drive_create(0, p_timing, &p_bbc->options);
  p_bbc->p_drive_1 = disc_drive_create(1, p_timing, &p_bbc->options);

  if (p_bbc->is_wd_fdc) {
    p_bbc->p_wd_fdc = wd_fdc_create(p_state_6502,
                                    is_master,
                                    p_bbc->is_wd_1772,
                                    p_timing,
                                    &p_bbc->options);
    wd_fdc_set_drives(p_bbc->p_wd_fdc, p_bbc->p_drive_0, p_bbc->p_drive_1);
  } else {
    p_bbc->p_intel_fdc = intel_fdc_create(p_state_6502,
                                          p_timing,
                                          &p_bbc->options);
    intel_fdc_set_drives(p_bbc->p_intel_fdc,
                         p_bbc->p_drive_0,
                         p_bbc->p_drive_1);
  }

  p_bbc->p_serial = mc6850_create(p_state_6502, &p_bbc->options);

  p_bbc->p_tape = tape_create(p_timing, &p_bbc->options);

  p_bbc->p_serial_ula = serial_ula_create(p_bbc->p_serial,
                                          p_bbc->p_tape,
                                          fasttape_flag,
                                          &p_bbc->options);
  serial_ula_set_fast_mode_callback(p_bbc->p_serial_ula,
                                    bbc_set_fast_mode_callback,
                                    (void*) p_bbc);

  /* Set up a virtual printer that prints to a file. */
  via_set_CA2_changed_callback(p_bbc->p_user_via,
                               bbc_CA2_changed_callback,
                               p_bbc);

  p_debug = debug_create(p_bbc, debug_flag, &p_bbc->options);

  p_bbc->p_debug = p_debug;
  p_bbc->options.p_debug_object = p_debug;

  p_bbc->p_cpu_driver = cpu_driver_alloc(mode,
                                         is_65c12,
                                         p_state_6502,
                                         &p_bbc->memory_access,
                                         p_timing,
                                         &p_bbc->options);
  cpu_driver_init(p_bbc->p_cpu_driver);
  p_bbc->p_cpu_driver->p_funcs->set_reset_callback(p_bbc->p_cpu_driver,
                                                   bbc_do_reset_callback,
                                                   p_bbc);

  debug_init(p_debug);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  volatile int* p_running = &p_bbc->running;
  volatile int* p_thread_allocated = &p_bbc->thread_allocated;

  (void) p_running;
  assert(!*p_running);

  if (p_bbc->p_printer_file != NULL) {
    util_file_close(p_bbc->p_printer_file);
  }

  if (*p_thread_allocated) {
    (void) os_thread_destroy(p_bbc->p_thread_cpu);
  }

  p_cpu_driver->p_funcs->destroy(p_cpu_driver);

  debug_destroy(p_bbc->p_debug);
  serial_ula_destroy(p_bbc->p_serial_ula);
  mc6850_destroy(p_bbc->p_serial);
  tape_destroy(p_bbc->p_tape);
  video_destroy(p_bbc->p_video);
  teletext_destroy(p_bbc->p_teletext);
  render_destroy(p_bbc->p_render);
  sound_destroy(p_bbc->p_sound);
  joystick_destroy(p_bbc->p_joystick);
  adc_destroy(p_bbc->p_adc);
  keyboard_destroy(p_bbc->p_keyboard);
  via_destroy(p_bbc->p_system_via);
  via_destroy(p_bbc->p_user_via);
  if (p_bbc->p_cmos != NULL) {
    cmos_destroy(p_bbc->p_cmos);
  }
  if (p_bbc->p_intel_fdc != NULL) {
    intel_fdc_destroy(p_bbc->p_intel_fdc);
  }
  if (p_bbc->p_wd_fdc != NULL) {
    wd_fdc_destroy(p_bbc->p_wd_fdc);
  }
  disc_drive_destroy(p_bbc->p_drive_0);
  disc_drive_destroy(p_bbc->p_drive_1);
  state_6502_destroy(p_bbc->p_state_6502);
  timing_destroy(p_bbc->p_timing);
  os_alloc_free_mapping(p_bbc->p_mapping_raw);
  os_alloc_free_mapping(p_bbc->p_mapping_read);
  os_alloc_free_mapping(p_bbc->p_mapping_write);
  os_alloc_free_mapping(p_bbc->p_mapping_write_2);
  if (p_bbc->p_mapping_read_ind != NULL) {
    os_alloc_free_mapping(p_bbc->p_mapping_read_ind);
    os_alloc_free_mapping(p_bbc->p_mapping_write_ind);
    os_alloc_free_mapping(p_bbc->p_mapping_write_ind_2);
  }
  os_alloc_free_memory_handle(p_bbc->mem_handle);

  os_time_free_sleeper(p_bbc->p_sleeper);

  util_free(p_bbc->p_mem_sideways);
  util_free(p_bbc->p_mem_master);
  util_free(p_bbc);
}

void
bbc_focus_lost_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  keyboard_release_all_physical_keys(p_bbc->p_keyboard);
}

void
bbc_enable_extended_rom_addressing(struct bbc_struct* p_bbc) {
  p_bbc->is_extended_rom_addressing = 1;
  p_bbc->is_romsel_invalidated = 1;
  bbc_sideways_select(p_bbc, p_bbc->romsel);
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

static void
bbc_power_on_memory_reset(struct bbc_struct* p_bbc) {
  uint32_t i;

  uint8_t* p_mem_raw = p_bbc->p_mem_raw;
  uint8_t* p_mem_sideways = p_bbc->p_mem_sideways;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  /* Clear memory. */
  /* EMU NOTE: skullduggery! On a lot of BBCs, the power-on DRAM state is 0xFF,
   * and Eagle Empire was even found to depend on this otherwise it is crashy.
   * On the flipside, Clogger appears to rely on a power-on DRAM value of 0x00
   * in the zero page.
   * We cater for both of these quirks below.
   * Full story: https://github.com/mattgodbolt/jsbeeb/issues/105
   */
  (void) memset(p_mem_raw, '\xFF', k_bbc_sideways_offset);
  (void) memset(p_mem_raw, '\0', 0x100);

  /* Clear sideways memory, if any. */
  for (i = 0; i < k_bbc_num_roms; ++i) {
    if (!p_bbc->is_sideways_ram_bank[i]) {
      continue;
    }
    (void) memset((p_mem_sideways + (i * k_bbc_rom_size)),
                  '\0',
                  k_bbc_rom_size);
  }

  /* Clear Master memory. */
  if (p_bbc->is_master) {
    (void) memset(p_bbc->p_mem_lynne, '\0', k_bbc_lynne_size);
    (void) memset(p_bbc->p_mem_hazel, '\0', k_bbc_hazel_size);
    (void) memset(p_bbc->p_mem_andy, '\0', k_bbc_andy_size);

    (void) bbc_set_acccon(p_bbc, 0);
  }

  p_bbc->is_romsel_invalidated = 1;
  bbc_sideways_select(p_bbc, 0);

  p_cpu_driver->p_funcs->memory_range_invalidate(p_cpu_driver,
                                                 0,
                                                 k_6502_addr_space_size);
}

static void
bbc_power_on_other_reset(struct bbc_struct* p_bbc) {
  p_bbc->IC32 = 0;

  bbc_reset_callback_baselines(p_bbc);

  /* TODO: decide if the stop cycles timer should be reset or not. */
}

void
bbc_power_on_reset(struct bbc_struct* p_bbc) {
  struct timing_struct* p_timing = p_bbc->p_timing;
  struct keyboard_struct* p_keyboard = p_bbc->p_keyboard;
  int32_t timer_id_autoboot = p_bbc->timer_id_autoboot;

  if (timer_id_autoboot != -1) {
    if (timing_timer_is_running(p_timing, timer_id_autoboot)) {
      (void) timing_stop_timer(p_timing, timer_id_autoboot);
    }
  }

  timing_reset_total_timer_ticks(p_timing);
  bbc_power_on_memory_reset(p_bbc);
  bbc_power_on_other_reset(p_bbc);
  assert(p_bbc->romsel == 0);
  assert(p_bbc->acccon == 0);
  assert(!p_bbc->is_acccon_usr_mos_different);
  assert(p_bbc->is_romsel_invalidated == 0);
  via_power_on_reset(p_bbc->p_system_via);
  via_power_on_reset(p_bbc->p_user_via);
  /* For our virtual printer to indicate ready. */
  via_set_CA1(p_bbc->p_user_via, 1);
  sound_power_on_reset(p_bbc->p_sound);
  /* Reset serial before the tape so that playing has been stopped. */
  mc6850_power_on_reset(p_bbc->p_serial);
  serial_ula_power_on_reset(p_bbc->p_serial_ula);
  tape_power_on_reset(p_bbc->p_tape);
  /* Reset the controller before the drives so that spindown has been done. */
  if (p_bbc->p_intel_fdc != NULL) {
    intel_fdc_power_on_reset(p_bbc->p_intel_fdc);
  }
  if (p_bbc->p_wd_fdc != NULL) {
    wd_fdc_power_on_reset(p_bbc->p_wd_fdc);
  }
  disc_drive_power_on_reset(p_bbc->p_drive_0);
  disc_drive_power_on_reset(p_bbc->p_drive_1);
  keyboard_power_on_reset(p_keyboard);
  video_power_on_reset(p_bbc->p_video);
  adc_power_on_reset(p_bbc->p_adc);

  /* Not reset: teletext, render. They don't affect execution (only display) and
   * will resync to the new display output pretty immediately.
   */

  assert(timing_get_total_timer_ticks(p_timing) == 0);
  state_6502_reset(p_bbc->p_state_6502);

  if (p_bbc->autoboot_flag) {
    keyboard_system_key_pressed(p_keyboard, k_keyboard_key_shift_left);
    /* 1.5s is enough for the Master Compact, which is slowest. */
    (void) timing_start_timer_with_value(p_timing, timer_id_autoboot, 3000000);
  }
}

struct cpu_driver*
bbc_get_cpu_driver(struct bbc_struct* p_bbc) {
  return p_bbc->p_cpu_driver;
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

struct mc6850_struct*
bbc_get_serial(struct bbc_struct* p_bbc) {
  return p_bbc->p_serial;
}

struct serial_ula_struct*
bbc_get_serial_ula(struct bbc_struct* p_bbc) {
  return p_bbc->p_serial_ula;
}

struct cmos_struct*
bbc_get_cmos(struct bbc_struct* p_bbc) {
  return p_bbc->p_cmos;
}

struct timing_struct*
bbc_get_timing(struct bbc_struct* p_bbc) {
  return p_bbc->p_timing;
}

struct wd_fdc_struct*
bbc_get_wd_fdc(struct bbc_struct* p_bbc) {
  return p_bbc->p_wd_fdc;
}

struct disc_drive_struct*
bbc_get_drive_0(struct bbc_struct* p_bbc) {
  return p_bbc->p_drive_0;
}

struct disc_drive_struct*
bbc_get_drive_1(struct bbc_struct* p_bbc) {
  return p_bbc->p_drive_1;
}

struct adc_struct*
bbc_get_adc(struct bbc_struct* p_bbc) {
  return p_bbc->p_adc;
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

void
bbc_get_address_details(struct bbc_struct* p_bbc,
                        int* p_out_is_register,
                        int* p_out_is_rom,
                        uint16_t addr_6502) {
  /* 0x0000 - 0x7FFF */
  if (addr_6502 < k_bbc_sideways_offset) {
    *p_out_is_register = 0;
    *p_out_is_rom = 0;
    return;
  }
  /* 0x8000 - 0xBFFF */
  if (addr_6502 < k_bbc_os_rom_offset) {
    uint8_t bank = bbc_get_effective_bank(p_bbc, p_bbc->romsel);
    *p_out_is_register = 0;
    if (p_bbc->is_sideways_ram_bank[bank]) {
      *p_out_is_rom = 0;
      return;
    }
    if (p_bbc->is_master &&
        (p_bbc->romsel & k_romsel_andy) &&
        (addr_6502 < (k_bbc_sideways_offset + k_bbc_andy_size))) {
      *p_out_is_rom = 0;
      return;
    }
    *p_out_is_rom = 1;
    return;
  }
  /* 0xC000 - 0xFFFF */
  if ((addr_6502 >= k_bbc_registers_start) &&
      (addr_6502 < ((k_bbc_registers_start + k_bbc_registers_len)))) {
    *p_out_is_register = 1;
    *p_out_is_rom = 0;
    return;
  }
  *p_out_is_register = 0;
  if (p_bbc->is_master &&
      (p_bbc->acccon & k_acccon_hazel) &&
      (addr_6502 < (k_bbc_os_rom_offset + k_bbc_hazel_size))) {
    *p_out_is_rom = 0;
    return;
  }
  *p_out_is_rom = 1;
}

int
bbc_get_run_flag(struct bbc_struct* p_bbc) {
  return p_bbc->run_flag;
}

int
bbc_get_print_flag(struct bbc_struct* p_bbc) {
  return p_bbc->print_flag;
}

static void
bbc_do_sleep(struct bbc_struct* p_bbc,
             uint64_t last_time_us,
             uint64_t curr_time_us,
             uint64_t delta_us) {
  uint64_t next_wakeup_time_us = (last_time_us + delta_us);
  int64_t spare_time_us = (next_wakeup_time_us - curr_time_us);

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

int
bbc_replay_seek(struct bbc_struct* p_bbc, uint64_t seek_target) {
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  if (!keyboard_can_rewind(p_bbc->p_keyboard)) {
    return 0;
  }

  p_bbc->rewind_to_cycles = seek_target;

  p_cpu_driver->p_funcs->apply_flags(
      p_cpu_driver,
      (k_cpu_flag_hard_reset | k_cpu_flag_replay),
      0);

  return 1;
}

static inline void
bbc_check_alt_keys(struct bbc_struct* p_bbc) {
  struct keyboard_struct* p_keyboard = p_bbc->p_keyboard;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  if (keyboard_consume_key_press(p_keyboard, k_keyboard_key_home)) {
    /* Trigger debugger. */
    volatile int* p_debug_interrupt = debug_get_interrupt(p_bbc->p_debug);
    *p_debug_interrupt = 1;
  }

  if (keyboard_consume_alt_key_press(p_keyboard, 'F')) {
    /* Toggle fast mode. */
    bbc_set_fast_flag(p_bbc, !p_bbc->fast_flag);
  } else if (keyboard_consume_alt_key_press(p_keyboard, 'E')) {
    /* Exit any in progress replay. */
    if (keyboard_is_replaying(p_keyboard)) {
      keyboard_end_replay(p_keyboard);
    }
  } else if (keyboard_consume_alt_key_press(p_keyboard, '0')) {
    disc_drive_cycle_disc(p_bbc->p_drive_0);
  } else if (keyboard_consume_alt_key_press(p_keyboard, '1')) {
    disc_drive_cycle_disc(p_bbc->p_drive_1);
  } else if (keyboard_consume_alt_key_press(p_keyboard, 'T')) {
    tape_cycle_tape(p_bbc->p_tape);
  } else if (keyboard_consume_alt_key_press(p_keyboard, 'R')) {
    /* We're in the middle of some timer callback. Let the CPU driver initiate
     * the actual reset at a safe time.
     */
    p_cpu_driver->p_funcs->apply_flags(p_cpu_driver, k_cpu_flag_hard_reset, 0);
  } else if (keyboard_consume_alt_key_press(p_keyboard, 'Z')) {
    int64_t seek_target = timing_get_total_timer_ticks(p_bbc->p_timing);
    /* "undo" -- go back 5 seconds if there is a current capture or replay. */
    /* TODO: not correct for non-2MHz tick rates. */
    seek_target -= (5 * 2000000);
    if (seek_target < 0) {
      seek_target = 0;
    }

    (void) bbc_replay_seek(p_bbc, seek_target);
  }
}

static void
bbc_cycles_timer_callback(void* p) {
  uint64_t delta_us;
  uint64_t cycles_next_run;
  int64_t refreshed_time;
  uint64_t curr_time_us;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct timing_struct* p_timing = p_bbc->p_timing;
  struct keyboard_struct* p_keyboard = p_bbc->p_keyboard;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;
  uint64_t last_time_us = p_bbc->last_time_us;

  /* Pull physical key events from system thread, always.
   * If this ends up updating the virtual keyboard, this call also syncs
   * interrupts and checks for BREAK.
   */
  keyboard_read_queue(p_keyboard);

  /* Check for special alt key combos to change emulator behavior. */
  bbc_check_alt_keys(p_bbc);

  curr_time_us = os_time_get_us();
  p_bbc->last_time_us = curr_time_us;

  if (p_bbc->log_timestamp) {
    log_do_log(k_log_perf,
               k_log_info,
               "time delta: %"PRIu64, (curr_time_us - last_time_us));
  }

  if (!p_bbc->fast_flag) {
    struct sound_struct* p_sound = p_bbc->p_sound;
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

    /* If sound is active, we use that as a source of timed wait, otherwise it's
     * a dedicated sleep.
     */
    if (sound_is_synchronous(p_sound)) {
      /* There's no sound output (which would block!) in fast mode, so we put it
       * here in the slow path. All of the potentially blocking calls are
       * localized to the slow path.
       */
      sound_tick(p_sound, curr_time_us);
    } else {
      /* This may adjust p_bbc->last_time_us to maintain smooth timing. */
      bbc_do_sleep(p_bbc, last_time_us, curr_time_us, delta_us);
    }
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
  adc_apply_wall_time_delta(p_bbc->p_adc, delta_us);

  /* TODO: this is pretty poor. The serial device should maintain its own
   * timer at the correct baud rate for the externally attached device.
   */
  serial_ula_tick(p_bbc->p_serial_ula);

  joystick_tick(p_bbc->p_joystick);

  if (p_bbc->log_speed) {
    bbc_do_log_speed(p_bbc, curr_time_us);
  }

  p_cpu_driver->p_funcs->housekeeping_tick(p_cpu_driver);
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

  /* Set up initial fast mode correctly. */
  bbc_set_fast_flag(p_bbc, p_bbc->fast_flag);

  exited = p_cpu_driver->p_funcs->enter(p_cpu_driver);
  (void) exited;
  assert(exited == 1);
  assert(p_cpu_driver->p_funcs->get_flags(p_cpu_driver) & k_cpu_flag_exited);

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
bbc_add_disc(struct bbc_struct* p_bbc,
             const char* p_filename,
             int drive,
             int is_writeable,
             int is_mutable,
             int convert_to_hfe,
             int convert_to_ssd,
             int convert_to_adl) {
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
                       convert_to_ssd,
                       convert_to_adl,
                       &p_bbc->options);
  if (p_disc == NULL) {
    util_bail("disc_create failed");
  }

  disc_drive_add_disc(p_drive, p_disc);
}

void
bbc_add_raw_disc(struct bbc_struct* p_bbc,
                 const char* p_filename,
                 const char* p_spec) {
  struct disc_struct* p_disc = disc_create_from_raw(p_filename, p_spec);

  if (p_disc == NULL) {
    util_bail("disc_create_from_raw failed");
  }

  disc_drive_add_disc(p_bbc->p_drive_0, p_disc);
}

void
bbc_add_tape(struct bbc_struct* p_bbc, const char* p_file_name) {
  tape_add_tape(p_bbc->p_tape, p_file_name);
}

static void
bbc_stop_cycles_timer_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct cpu_driver* p_cpu_driver = p_bbc->p_cpu_driver;

  (void) timing_stop_timer(p_bbc->p_timing, p_bbc->timer_id_stop_cycles);

  p_cpu_driver->p_funcs->apply_flags(p_cpu_driver, k_cpu_flag_exited, 0);
  p_cpu_driver->p_funcs->set_exit_value(p_cpu_driver, 0xFFFFFFFE);
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

static void
bbc_autoboot_timer_callback(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;
  struct keyboard_struct* p_keyboard = p_bbc->p_keyboard;

  (void) timing_stop_timer(p_bbc->p_timing, p_bbc->timer_id_autoboot);

  keyboard_system_key_released(p_keyboard, k_keyboard_key_shift_left);
}

void
bbc_set_autoboot(struct bbc_struct* p_bbc, int autoboot_flag) {
  if (autoboot_flag && (p_bbc->timer_id_autoboot == -1)) {
    p_bbc->timer_id_autoboot =
        timing_register_timer(p_bbc->p_timing,
                              bbc_autoboot_timer_callback,
                              p_bbc);
  }
  p_bbc->autoboot_flag = autoboot_flag;
}

void
bbc_set_commands(struct bbc_struct* p_bbc, const char* p_commands) {
  debug_set_commands(p_bbc->p_debug, p_commands);
}

#include "test-bbc.c"
