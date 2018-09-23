#include "bbc.h"

#include "debug.h"
#include "jit.h"
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
#include <time.h>

static const size_t k_os_rom_offset = 0xc000;
static const size_t k_lang_rom_offset = 0x8000;

static const size_t k_us_per_timer_tick = 1000; /* 1ms / 1kHz */

enum {
  k_addr_crtc = 0xfe00,
  k_addr_acia = 0xfe08,
  k_addr_serial_ula = 0xfe10,
  k_addr_video_ula = 0xfe20,
  k_addr_rom_latch = 0xfe30,
  k_addr_sysvia = 0xfe40,
  k_addr_uservia = 0xfe60,
  k_addr_adc = 0xfec0,
  k_addr_tube = 0xfee0,
};
enum {
  k_crtc_address = 0x0,
  k_crtc_data = 0x1,
};
enum {
  k_video_ula_control = 0x0,
  k_video_ula_palette = 0x1,
};

struct bbc_struct {
  pthread_t thread;
  pthread_t timer_thread;
  int exited;
  size_t time_in_us;
  unsigned char* p_os_rom;
  unsigned char* p_lang_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  int slow_flag;
  unsigned char* p_mem;
  struct video_struct* p_video;
  struct via_struct* p_system_via;
  struct via_struct* p_user_via;
  struct jit_struct* p_jit;
  struct debug_struct* p_debug;

  unsigned char keys[16][16];
  unsigned char keys_count;
  unsigned char keys_count_col[16];
};

struct bbc_struct*
bbc_create(unsigned char* p_os_rom,
           unsigned char* p_lang_rom,
           int debug_flag,
           int run_flag,
           int print_flag,
           int slow_flag,
           const char* p_opt_flags,
           const char* p_log_flags,
           uint16_t debug_stop_addr) {
  struct debug_struct* p_debug;
  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }
  memset(p_bbc, '\0', sizeof(struct bbc_struct));

  p_bbc->exited = 0;
  p_bbc->time_in_us = 0;
  p_bbc->p_os_rom = p_os_rom;
  p_bbc->p_lang_rom = p_lang_rom;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;
  p_bbc->slow_flag = slow_flag;

  p_bbc->p_mem =
      util_get_guarded_mapping((unsigned char*) (size_t) k_bbc_mem_mmap_addr,
                               k_bbc_addr_space_size,
                               0);

  p_bbc->p_system_via = via_create(k_via_system, p_bbc);
  if (p_bbc->p_system_via == NULL) {
    errx(1, "via_create failed");
  }
  p_bbc->p_user_via = via_create(k_via_user, p_bbc);
  if (p_bbc->p_system_via == NULL) {
    errx(1, "via_create failed");
  }

  p_bbc->p_video = video_create(p_bbc->p_mem,
                                via_get_peripheral_b_ptr(p_bbc->p_system_via));
  if (p_bbc->p_video == NULL) {
    errx(1, "video_create failed");
  }

  p_debug = debug_create(p_bbc, debug_flag, debug_stop_addr);
  if (p_debug == NULL) {
    errx(1, "debug_create failed");
  }

  p_bbc->p_jit = jit_create(debug_callback,
                            p_debug,
                            p_bbc,
                            bbc_read_callback,
                            bbc_write_callback,
                            p_opt_flags,
                            p_log_flags);
  if (p_bbc->p_jit == NULL) {
    errx(1, "jit_create failed");
  }

  bbc_reset(p_bbc);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  jit_destroy(p_bbc->p_jit);
  debug_destroy(p_bbc->p_debug);
  video_destroy(p_bbc->p_video);
  util_free_guarded_mapping(p_bbc->p_mem, k_bbc_addr_space_size);
  free(p_bbc);
}

void
bbc_reset(struct bbc_struct* p_bbc) {
  unsigned char* p_mem = p_bbc->p_mem;
  unsigned char* p_os_start = p_mem + k_os_rom_offset;
  unsigned char* p_lang_start = p_mem + k_lang_rom_offset;
  struct jit_struct* p_jit = p_bbc->p_jit;
  uint16_t init_pc;

  /* Clear memory / ROMs. */
  memset(p_mem, '\0', k_bbc_addr_space_size);

  /* Copy in OS and language ROM. */
  memcpy(p_os_start, p_bbc->p_os_rom, k_bbc_rom_size);
  memcpy(p_lang_start, p_bbc->p_lang_rom, k_bbc_rom_size);

  util_make_mapping_read_only(p_mem + k_bbc_ram_size,
                              k_bbc_addr_space_size - k_bbc_ram_size);

  /* Initial 6502 state. */
  init_pc = p_mem[k_bbc_vector_reset] | (p_mem[k_bbc_vector_reset + 1] << 8);
  jit_set_registers(p_jit, 0, 0, 0, 0, 0x04, /* I flag */ init_pc);
}

void
bbc_get_registers(struct bbc_struct* p_bbc,
                  unsigned char* a,
                  unsigned char* x,
                  unsigned char* y,
                  unsigned char* s,
                  unsigned char* flags,
                  uint16_t* pc) {
  struct jit_struct* p_jit = p_bbc->p_jit;
  jit_get_registers(p_jit, a, x, y, s, flags, pc);
}

void
bbc_set_registers(struct bbc_struct* p_bbc,
                  unsigned char a,
                  unsigned char x,
                  unsigned char y,
                  unsigned char s,
                  unsigned char flags,
                  uint16_t pc) {
  struct jit_struct* p_jit = p_bbc->p_jit;
  jit_set_registers(p_jit, a, x, y, s, flags, pc);
}

uint16_t
bbc_get_block(struct bbc_struct* p_bbc, uint16_t reg_pc) {
  struct jit_struct* p_jit = p_bbc->p_jit;
  return jit_block_from_6502(p_jit, reg_pc);
}

void
bbc_check_pc(struct bbc_struct* p_bbc) {
  struct jit_struct* p_jit = p_bbc->p_jit;
  jit_check_pc(p_jit);
}

struct via_struct*
bbc_get_sysvia(struct bbc_struct* p_bbc) {
  return p_bbc->p_system_via;
}

struct via_struct*
bbc_get_uservia(struct bbc_struct* p_bbc) {
  return p_bbc->p_user_via;
}

struct jit_struct*
bbc_get_jit(struct bbc_struct* p_bbc) {
  return p_bbc->p_jit;
}

struct video_struct*
bbc_get_video(struct bbc_struct* p_bbc) {
  return p_bbc->p_video;
}

unsigned char*
bbc_get_mem(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem;
}

void
bbc_set_memory_block(struct bbc_struct* p_bbc,
                     uint16_t addr,
                     uint16_t len,
                     unsigned char* p_src_mem) {
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
                 unsigned char val) {
  unsigned char* p_mem = p_bbc->p_mem;
  struct jit_struct* p_jit = p_bbc->p_jit;

  /* Allow a forced write to ROM using this API -- need to flip memory
   * protections.
   */
  if (addr_6502 >= k_bbc_ram_size) {
    util_make_mapping_read_write(p_mem + k_bbc_ram_size,
                                 k_bbc_addr_space_size - k_bbc_ram_size);
  }

  p_mem[addr_6502] = val;

  if (addr_6502 >= k_bbc_ram_size) {
    util_make_mapping_read_only(p_mem + k_bbc_ram_size,
                                k_bbc_addr_space_size - k_bbc_ram_size);
  }


  jit_memory_written(p_jit, addr_6502);
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
bbc_get_slow_flag(struct bbc_struct* p_bbc) {
  return p_bbc->slow_flag;
}

static void
bbc_async_timer_tick(struct bbc_struct* p_bbc) {
  /* TODO: this timer ticks at 1kHz and interrupts the JIT process at the same
   * rate. But we only need to interrupt the JIT process if there's something
   * to do, which would improve performance.
   * The "cost" would be needing to be very careful with the async nature of
   * this thread.
   * Alternatively: dynamically adjust the timer tick based on the suite of
   * timers and when the next one is expected to expire.
   */
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);
  jit_async_timer_tick(p_jit);
}

static void*
bbc_timer_thread(void* p) {
  struct timespec ts;
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  ts.tv_sec = 0;
  ts.tv_nsec = k_us_per_timer_tick * 1000;

  while (1) {
    int ret = nanosleep(&ts, NULL);
    /* TODO: cope with signal interruption? */
    if (ret != 0) {
      errx(1, "nanosleep failed");
    }
    if (p_bbc->exited) {
      break;
    }
    bbc_async_timer_tick(p_bbc);
  }

  return NULL;
}

static void
bbc_start_timer_tick(struct bbc_struct* p_bbc) {
  int ret = pthread_create(&p_bbc->timer_thread, NULL, bbc_timer_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create timer thread");
  }
}

static void*
bbc_jit_thread(void* p) {
  int ret;

  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  bbc_start_timer_tick(p_bbc);

  jit_enter(p_bbc->p_jit);

  p_bbc->exited = 1;

  ret = pthread_join(p_bbc->timer_thread, NULL);
  if (ret != 0) {
    errx(1, "pthread_join failed");
  }

  return NULL;
}

void
bbc_sync_timer_tick(struct bbc_struct* p_bbc) {
  assert((k_us_per_timer_tick % 1000) == 0);

  p_bbc->time_in_us += k_us_per_timer_tick;

  /* 100Hz sysvia timer. */
  via_time_advance(p_bbc->p_system_via, k_us_per_timer_tick);
  via_time_advance(p_bbc->p_user_via, k_us_per_timer_tick);

  /* Fire vsync at 50Hz. */
  if (!(p_bbc->time_in_us % 20000)) {
    via_raise_interrupt(p_bbc->p_system_via, k_int_CA1);
  }

  /* Read sysvia port A to update keyboard state and fire interrupts. */
  (void) via_read(p_bbc->p_system_via, k_via_ORAnh);
}

void
bbc_run_async(struct bbc_struct* p_bbc) {
  int ret = pthread_create(&p_bbc->thread, NULL, bbc_jit_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create jit thread");
  }
}

int
bbc_has_exited(struct bbc_struct* p_bbc) {
  /* TODO: should use pthread_tryjoin_np? */
  return p_bbc->exited;
}

void
bbc_set_interrupt(struct bbc_struct* p_bbc, int id, int set) {
  jit_set_interrupt(p_bbc->p_jit, id, set);
}

int
bbc_is_ram_address(struct bbc_struct* p_bbc, uint16_t addr) {
  return (addr < k_bbc_ram_size);
}

int
bbc_is_special_read_address(struct bbc_struct* p_bbc,
                            uint16_t addr_low,
                            uint16_t addr_high) {
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
  if (addr_low >= k_lang_rom_offset) {
    return 1;
  }
  if (addr_high >= k_lang_rom_offset) {
    return 1;
  }
  return 0;
}

unsigned char
bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr) {
  /* We have an imprecise match for abx and aby addressing modes so we may get
   * here with a non-registers address.
   */
  if (addr < k_bbc_registers_start ||
      addr >= k_bbc_registers_start + k_bbc_registers_len) {
    unsigned char* p_mem = bbc_get_mem(p_bbc);
    return p_mem[addr];
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
  case k_addr_adc:
    /* No ADC attention needed (bit 6). */
    return 0;
  case k_addr_tube:
    /* Not present -- fall through to return 0xfe. */
    break;
  case 0xFE18:
    /* Only used in Master model but read by Synchron. */
    break;
  default:
    printf("unknown read: %x\n", addr);
    assert(0);
  }
  return 0xfe;
}

void
bbc_write_callback(struct bbc_struct* p_bbc, uint16_t addr, unsigned char val) {
  struct video_struct* p_video = bbc_get_video(p_bbc);
  /* We bounce here for ROM writes as well as register writes; ROM writes
   * are simply squashed.
   */
  if (addr < k_bbc_registers_start ||
      addr >= k_bbc_registers_start + k_bbc_registers_len) {
    return;
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
  case k_addr_crtc | k_crtc_address:
    video_set_crtc_address(p_video, val);
    break;
  case k_addr_crtc | k_crtc_data:
    video_set_crtc_data(p_video, val);
    break;
  case k_addr_acia:
    printf("ignoring ACIA write\n");
    break;
  case k_addr_serial_ula:
    printf("ignoring serial ULA write\n");
    break;
  case k_addr_video_ula | k_video_ula_control:
    video_set_ula_control(p_video, val);
    break;
  case k_addr_video_ula | k_video_ula_palette:
    video_set_ula_palette(p_video, val);
    break;
  case k_addr_rom_latch:
    printf("ignoring ROM latch write\n");
    break;
  case k_addr_adc:
    printf("ignoring ADC write\n");
    break;
  case k_addr_tube:
    printf("ignoring tube write\n");
    break;
  default:
    printf("unknown write: %x\n", addr);
    assert(0);
  }
}

static void
bbc_key_to_rowcol(int key, int* p_row, int* p_col) {
  int row = -1;
  int col = -1;
  switch (key) {
  case 9: /* Escape */
    row = 7;
    col = 0;
    break;
  case 10: /* 1 */
    row = 3;
    col = 0;
    break;
  case 11: /* 2 */
    row = 3;
    col = 1;
    break;
  case 12: /* 3 */
    row = 1;
    col = 1;
    break;
  case 13: /* 4 */
    row = 1;
    col = 2;
    break;
  case 14: /* 5 */
    row = 1;
    col = 3;
    break;
  case 15: /* 6 */
    row = 3;
    col = 4;
    break;
  case 16: /* 7 */
    row = 2;
    col = 4;
    break;
  case 17: /* 8 */
    row = 1;
    col = 5;
    break;
  case 18: /* 9 */
    row = 2;
    col = 6;
    break;
  case 19: /* 0 */
    row = 2;
    col = 7;
    break;
  case 20: /* - */
    row = 1;
    col = 7;
    break;
  case 21: /* = (BBC ^) */
    row = 1;
    col = 8;
    break;
  case 22: /* Backspace / (BBC DELETE) */
    row = 5;
    col = 9;
    break;
  case 23: /* Tab */
    row = 6;
    col = 0;
    break;
  case 24: /* Q */
    row = 1;
    col = 0;
    break;
  case 25: /* W */
    row = 2;
    col = 1;
    break;
  case 26: /* E */
    row = 2;
    col = 2;
    break;
  case 27: /* R */
    row = 3;
    col = 3;
    break;
  case 28: /* T */
    row = 2;
    col = 3;
    break;
  case 29: /* Y */
    row = 4;
    col = 4;
    break;
  case 30: /* U */
    row = 3;
    col = 5;
    break;
  case 31: /* I */
    row = 2;
    col = 5;
    break;
  case 32: /* O */
    row = 3;
    col = 6;
    break;
  case 33: /* P */
    row = 3;
    col = 7;
    break;
  case 34: /* [ (BBC @) */
    row = 4;
    col = 7;
    break;
  case 35: /* ] (BBC [) */
    row = 3;
    col = 8;
    break;
  case 36: /* Enter (BBC RETURN) */
    row = 4;
    col = 9;
    break;
  case 37: /* Ctrl */
    row = 0;
    col = 1;
    break;
  case 38: /* A */
    row = 4;
    col = 1;
    break;
  case 39: /* S */
    row = 5;
    col = 1;
    break;
  case 40: /* D */
    row = 3;
    col = 2;
    break;
  case 41: /* F */
    row = 4;
    col = 3;
    break;
  case 42: /* G */
    row = 5;
    col = 3;
    break;
  case 43: /* H */
    row = 5;
    col = 4;
    break;
  case 44: /* J */
    row = 4;
    col = 5;
    break;
  case 45: /* K */
    row = 4;
    col = 6;
    break;
  case 46: /* L */
    row = 5;
    col = 6;
    break;
  case 47: /* ; */
    row = 5;
    col = 7;
    break;
  case 48: /* ' (BBC colon) */
    row = 4;
    col = 8;
    break;
  case 50: /* Left shift */
    row = 0;
    col = 0;
    break;
  case 51: /* \ (BBC ]) */
    row = 5;
    col = 8;
    break;
  case 52: /* Z */
    row = 6;
    col = 1;
    break;
  case 53: /* X */
    row = 4;
    col = 2;
    break;
  case 54: /* C */
    row = 5;
    col = 2;
    break;
  case 55: /* V */
    row = 6;
    col = 3;
    break;
  case 56: /* B */
    row = 6;
    col = 4;
    break;
  case 57: /* N */
    row = 5;
    col = 5;
    break;
  case 58: /* M */
    row = 6;
    col = 5;
    break;
  case 59: /* , */
    row = 6;
    col = 6;
    break;
  case 60: /* . */
    row = 6;
    col = 7;
    break;
  case 61: /* / */
    row = 6;
    col = 8;
    break;
  case 62: /* Right shift */
    row = 0;
    col = 0;
    break;
  case 65: /* Space */
    row = 6;
    col = 2;
    break;
  case 66: /* Caps Lock */
    row = 4;
    col = 0;
    break;
  case 111: /* Up arrow */
    row = 3;
    col = 9;
    break;
  case 113: /* Left arrow */
    row = 1;
    col = 9;
    break;
  case 114: /* Right arrow */
    row = 7;
    col = 9;
    break;
  case 116: /* Down arrow */
    row = 2;
    col = 9;
    break;
  default:
    printf("warning: unhandled key %d\n", key);
    break;
  }

  *p_row = row;
  *p_col = col;
}

void
bbc_key_pressed(struct bbc_struct* p_bbc, int key) {
  /* Threading model: called from the X thread.
   * Allowed to read/write keyboard state.
   */
  int row;
  int col;
  bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  if (p_bbc->keys[row][col]) {
    return;
  }
  p_bbc->keys[row][col] = 1;
  p_bbc->keys_count_col[col]++;
  p_bbc->keys_count++;
}

void
bbc_key_released(struct bbc_struct* p_bbc, int key) {
  /* Threading model: called from the X thread.
   * Allowed to read/write keyboard state.
   * There's no other writer thread, so updates (such as increment / decrement
   * of counters) don't need to be atomic. However, the reader should be aware
   * that keyboard state is changing asynchronously.
   * e.g. bbc_is_any_key_pressed() could return 0 but an immediately following
   * specific bbc_is_key_pressed() could return 1.
   */
  int row;
  int col;
  int was_pressed;
  bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  was_pressed = p_bbc->keys[row][col];
  p_bbc->keys[row][col] = 0;
  if (was_pressed) {
    assert(p_bbc->keys_count_col[col] > 0);
    p_bbc->keys_count_col[col]--;
    assert(p_bbc->keys_count > 0);
    p_bbc->keys_count--;
  }
}

int
bbc_is_key_pressed(struct bbc_struct* p_bbc,
                   unsigned char row,
                   unsigned char col) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  return p_bbc->keys[row][col];
}

int
bbc_is_key_column_pressed(struct bbc_struct* p_bbc, unsigned char col) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  return (p_bbc->keys_count_col[col] > 0);
}

int
bbc_is_any_key_pressed(struct bbc_struct* p_bbc) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  return (p_bbc->keys_count > 0);
}
