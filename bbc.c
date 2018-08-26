#include "bbc.h"

#include "debug.h"
#include "jit.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const size_t k_addr_space_size = 0x10000;
static void* k_mem_addr = (void*) 0x10000000;

static const size_t k_os_rom_offset = 0xc000;
static const size_t k_lang_rom_offset = 0x8000;
static const size_t k_mode7_offset = 0x7c00;
static const size_t k_mode45_offset = 0x5800;
static const size_t k_mode012_offset = 0x3000;

static const size_t k_registers_offset = 0xfc00;
static const size_t k_registers_len = 0x300;

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
enum {
  k_ula_teletext = 0x02,
  k_ula_chars_per_line = 0x0c,
  k_ula_chars_per_line_shift = 2,
  k_ula_clock_speed = 0x10,
  k_ula_clock_speed_shift = 4,
};
enum {
  k_via_ORB =   0x0,
  k_via_ORA =   0x1,
  k_via_DDRB =  0x2,
  k_via_DDRA =  0x3,
  k_via_T1CL =  0x4,
  k_via_T1CH =  0x5,
  k_via_T1LL =  0x6,
  k_via_T1LH =  0x7,
  k_via_SR =    0xa,
  k_via_ACR =   0xb,
  k_via_PCR =   0xc,
  k_via_IFR =   0xd,
  k_via_IER =   0xe,
  k_via_ORAnh = 0xf,
};
enum {
  k_int_CA2 =    0x01,
  k_int_TIMER1 = 0x40,
};

struct bbc_struct {
  unsigned char* p_os_rom;
  unsigned char* p_lang_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  int slow_flag;
  unsigned char* p_mem;
  struct jit_struct* p_jit;
  struct debug_struct* p_debug;

  unsigned char video_ula_control;

  unsigned char sysvia_ORB;
  unsigned char sysvia_ORA;
  unsigned char sysvia_DDRB;
  unsigned char sysvia_DDRA;
  unsigned char sysvia_T1CL;
  unsigned char sysvia_T1CH;
  unsigned char sysvia_T1LL;
  unsigned char sysvia_T1LH;
  unsigned char sysvia_SR;
  unsigned char sysvia_ACR;
  unsigned char sysvia_PCR;
  unsigned char sysvia_IFR;
  unsigned char sysvia_IER;
  unsigned char sysvia_IC32;
  unsigned char sysvia_sdb;
  unsigned char uservia_PCR;
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
           int slow_flag) {
  struct debug_struct* p_debug;
  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }
  memset(p_bbc, '\0', sizeof(struct bbc_struct));

  p_bbc->p_os_rom = p_os_rom;
  p_bbc->p_lang_rom = p_lang_rom;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;
  p_bbc->slow_flag = slow_flag;

  p_bbc->video_ula_control = 0;

  p_bbc->sysvia_ORB = 0;
  p_bbc->sysvia_ORA = 0;
  p_bbc->sysvia_DDRB = 0;
  p_bbc->sysvia_DDRA = 0;
  p_bbc->sysvia_T1CL = 0xff;
  p_bbc->sysvia_T1CH = 0xff;
  p_bbc->sysvia_T1LL = 0xff;
  p_bbc->sysvia_T1LH = 0xff;
  p_bbc->sysvia_SR = 0;
  p_bbc->sysvia_ACR = 0;
  p_bbc->sysvia_PCR = 0;
  p_bbc->sysvia_IFR = 0;
  p_bbc->sysvia_IER = 0;
  p_bbc->sysvia_IC32 = 0;
  p_bbc->sysvia_sdb = 0;
  p_bbc->uservia_PCR = 0;

  p_bbc->p_mem = util_get_guarded_mapping(k_mem_addr, k_addr_space_size, 0);

  p_debug = debug_create(p_bbc);
  if (p_debug == NULL) {
    errx(1, "debug_create failed");
  }

  p_bbc->p_jit = jit_create(k_mem_addr,
                            debug_callback,
                            p_debug,
                            p_bbc,
                            bbc_read_callback,
                            bbc_write_callback);
  if (p_bbc->p_jit == NULL) {
    errx(1, "jit_create failed");
  }

  jit_set_debug(p_bbc->p_jit, debug_flag);

  bbc_reset(p_bbc);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  jit_destroy(p_bbc->p_jit);
  debug_destroy(p_bbc->p_debug);
  util_free_guarded_mapping(p_bbc->p_mem, k_addr_space_size);
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
  memset(p_mem, '\0', k_addr_space_size);

  /* Copy in OS and language ROM. */
  memcpy(p_os_start, p_bbc->p_os_rom, k_bbc_rom_size);
  util_make_mapping_read_only(p_os_start, k_bbc_rom_size);

  memcpy(p_lang_start, p_bbc->p_lang_rom, k_bbc_rom_size);
  util_make_mapping_read_only(p_lang_start, k_bbc_rom_size);

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

void
bbc_get_sysvia(struct bbc_struct* p_bbc,
               unsigned char* sysvia_ORA,
               unsigned char* sysvia_ORB,
               unsigned char* sysvia_DDRA,
               unsigned char* sysvia_DDRB,
               unsigned char* sysvia_SR,
               unsigned char* sysvia_ACR,
               unsigned char* sysvia_PCR,
               unsigned char* sysvia_IFR,
               unsigned char* sysvia_IER,
               unsigned char* sysvia_IC32) {
  *sysvia_ORA = p_bbc->sysvia_ORA;
  *sysvia_ORB = p_bbc->sysvia_ORB;
  *sysvia_DDRA = p_bbc->sysvia_DDRA;
  *sysvia_DDRB = p_bbc->sysvia_DDRB;
  *sysvia_SR = p_bbc->sysvia_SR;
  *sysvia_ACR = p_bbc->sysvia_ACR;
  *sysvia_PCR = p_bbc->sysvia_PCR;
  *sysvia_IFR = p_bbc->sysvia_IFR;
  *sysvia_IER = p_bbc->sysvia_IER;
  *sysvia_IC32 = p_bbc->sysvia_IC32;
}

void
bbc_set_sysvia(struct bbc_struct* p_bbc,
               unsigned char sysvia_ORA,
               unsigned char sysvia_ORB,
               unsigned char sysvia_DDRA,
               unsigned char sysvia_DDRB,
               unsigned char sysvia_SR,
               unsigned char sysvia_ACR,
               unsigned char sysvia_PCR,
               unsigned char sysvia_IFR,
               unsigned char sysvia_IER,
               unsigned char sysvia_IC32) {
  p_bbc->sysvia_ORA = sysvia_ORA;
  p_bbc->sysvia_ORB = sysvia_ORB;
  p_bbc->sysvia_DDRA = sysvia_DDRA;
  p_bbc->sysvia_DDRB = sysvia_DDRB;
  p_bbc->sysvia_SR = sysvia_SR;
  p_bbc->sysvia_ACR = sysvia_ACR;
  p_bbc->sysvia_PCR = sysvia_PCR;
  p_bbc->sysvia_IFR = sysvia_IFR;
  p_bbc->sysvia_IER = sysvia_IER;
  p_bbc->sysvia_IC32 = sysvia_IC32;

  p_bbc->sysvia_sdb = 0;
}

struct jit_struct*
bbc_get_jit(struct bbc_struct* p_bbc) {
  return p_bbc->p_jit;
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

  p_mem[addr_6502] = val;

  jit_memory_written(p_jit, addr_6502);
}

unsigned char
bbc_get_video_ula_control(struct bbc_struct* p_bbc) {
  return p_bbc->video_ula_control;
}

void
bbc_set_video_ula_control(struct bbc_struct* p_bbc, unsigned char val) {
  p_bbc->video_ula_control = val;
}

unsigned char*
bbc_get_screen_mem(struct bbc_struct* p_bbc) {
  unsigned char ula_control = bbc_get_video_ula_control(p_bbc);
  size_t offset;
  if (ula_control & k_ula_teletext) {
    offset = k_mode7_offset;
  } else if (bbc_get_screen_clock_speed(p_bbc) == 1) {
    offset = k_mode012_offset;
  } else {
    offset = k_mode45_offset;
  }
  return p_bbc->p_mem + offset;
}

int
bbc_get_screen_is_text(struct bbc_struct* p_bbc) {
  unsigned char ula_control = bbc_get_video_ula_control(p_bbc);
  if (ula_control & k_ula_teletext) {
    return 1;
  }
  return 0;
}

size_t
bbc_get_screen_pixel_width(struct bbc_struct* p_bbc) {
  unsigned char ula_control = bbc_get_video_ula_control(p_bbc);
  unsigned char ula_chars_per_line = (ula_control & k_ula_chars_per_line) >>
                                         k_ula_chars_per_line_shift;
  return 1 << (3 - ula_chars_per_line);
}

size_t
bbc_get_screen_clock_speed(struct bbc_struct* p_bbc) {
  unsigned char ula_control = bbc_get_video_ula_control(p_bbc);
  unsigned char clock_speed = (ula_control & k_ula_clock_speed) >>
                                  k_ula_clock_speed_shift;
  return clock_speed;
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

static void*
bbc_jit_thread(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  jit_enter(p_bbc->p_jit);

  exit(0);
}

static void*
bbc_10ms_timer_thread(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  struct timespec ts;
  int ret;

  while (1) {
    ret = -1;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000 * 1000 * 10;
    while (ret == -1) {
      ret = nanosleep(&ts, &ts);
      assert(ret == 0 || ret == -1);
      if (ret == -1 && errno != EINTR) {
        errx(1, "nanosleep failed");
      }
    }
    bbc_fire_interrupt(p_bbc, 0, k_int_TIMER1);
  }
}

void
bbc_run_async(struct bbc_struct* p_bbc) {
  pthread_t thread;

  int ret = pthread_create(&thread, NULL, bbc_jit_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create jit thread");
  }
  ret = pthread_create(&thread, NULL, bbc_10ms_timer_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create timer thread");
  }
}

static void
bbc_check_interrupt(struct bbc_struct* p_bbc) {
  struct jit_struct* p_jit = p_bbc->p_jit;
  int interrupt;
  assert(!(p_bbc->sysvia_IER & 0x80));
  if (p_bbc->sysvia_IER & p_bbc->sysvia_IFR) {
    p_bbc->sysvia_IFR |= 0x80;
    interrupt = 1;
  } else {
    p_bbc->sysvia_IFR &= ~0x80;
    interrupt = 0;
  }
  jit_set_interrupt(p_jit, interrupt);
}

void
bbc_fire_interrupt(struct bbc_struct* p_bbc, int user, unsigned char bits) {
  assert(user == 0);
  assert(!(bits & 0x80));
  p_bbc->sysvia_IFR |= bits;
  bbc_check_interrupt(p_bbc);
}

int
bbc_is_special_read_address(struct bbc_struct* p_bbc,
                            uint16_t addr_low,
                            uint16_t addr_high) {
  if (addr_low >= k_registers_offset &&
      addr_low < k_registers_offset + k_registers_len) {
    return 1;
  }
  if (addr_high >= k_registers_offset &&
      addr_high < k_registers_offset + k_registers_len) {
    return 1;
  }
  if (addr_low < k_registers_offset &&
      addr_high >= k_registers_offset + k_registers_len) {
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

static void
bbc_sysvia_update_sdb(struct bbc_struct* p_bbc) {
  unsigned char sdb = p_bbc->sysvia_sdb;
  unsigned char keyrow = (sdb >> 4) & 7;
  unsigned char keycol = sdb & 0xf;
  int fire = 0;
  if (!(p_bbc->sysvia_IC32 & 8)) {
    if (!p_bbc->keys[keyrow][keycol]) {
      p_bbc->sysvia_sdb &= 0x7f;
    }
    if (p_bbc->keys_count_col[keycol]) {
      fire = 1;
    }
  } else {
    if (p_bbc->keys_count > 0) {
      fire = 1;
    }
  }
  if (fire) {
    bbc_fire_interrupt(p_bbc, 0, k_int_CA2);
  }
}

static unsigned char
bbc_sysvia_read_porta(struct bbc_struct* p_bbc) {
  bbc_sysvia_update_sdb(p_bbc);
/*  printf("sysvia sdb read %x\n", p_bbc->sysvia_sdb); */
  return p_bbc->sysvia_sdb;
}

static void
bbc_sysvia_write_porta(struct bbc_struct* p_bbc) {
  unsigned char via_ora = p_bbc->sysvia_ORA;
  unsigned char via_ddra = p_bbc->sysvia_DDRA;
  unsigned char sdb = (via_ora & via_ddra) | ~via_ddra;
  p_bbc->sysvia_sdb = sdb;
/*  unsigned char keyrow = (sdb >> 4) & 7;
  unsigned char keycol = sdb & 0xf;
  printf("sysvia sdb write val %x keyrow %d keycol %d\n",
         sdb,
         keyrow,
         keycol);*/
  bbc_sysvia_update_sdb(p_bbc);
}

static void
bbc_sysvia_write_portb(struct bbc_struct* p_bbc) {
  unsigned char via_orb = p_bbc->sysvia_ORB;
  unsigned char via_ddrb = p_bbc->sysvia_DDRB;
  unsigned char portb_val = (via_orb & via_ddrb) | ~via_ddrb;
  if (portb_val & 8) {
    p_bbc->sysvia_IC32 |= (1 << (portb_val & 7));
  } else {
    p_bbc->sysvia_IC32 &= ~(1 << (portb_val & 7));
  }
/*  printf("sysvia IC32 orb %x ddrb %x portb %x, new value %x\n",
         via_orb,
         via_ddrb,
         portb_val,
         p_bbc->sysvia_IC32);*/
}

unsigned char
bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr) {
  unsigned char val;
  unsigned char ora;
  unsigned char ddra;
  unsigned char orb;
  unsigned char ddrb;
  /* We have an imprecise match for abx and aby addressing modes so we may get
   * here with a non-registers address.
   */
  if (addr < k_registers_offset ||
      addr >= k_registers_offset + k_registers_len) {
    unsigned char* p_mem = bbc_get_mem(p_bbc);
    return p_mem[addr];
  }

  switch (addr) {
  case k_addr_acia:
    /* No ACIA interrupt (bit 7). */
    return 0;
  case k_addr_sysvia | k_via_ORB:
    assert((p_bbc->sysvia_PCR & 0xa0) != 0x20);
    assert(!(p_bbc->sysvia_ACR & 0x02));
    orb = p_bbc->sysvia_ORB;
    ddrb = p_bbc->sysvia_DDRB;
    val = orb & ddrb;
    /* Read is for joystick and CMOS. 0xff means nothing interesting. */
    val |= (0xff & ~ddrb);
    return val;
  case k_addr_sysvia | k_via_T1CL:
    return p_bbc->sysvia_T1CL;
  case k_addr_sysvia | k_via_SR:
    return p_bbc->sysvia_SR;
  case k_addr_sysvia | k_via_IFR:
    return p_bbc->sysvia_IFR;
  case k_addr_sysvia | k_via_IER:
    return p_bbc->sysvia_IER | 0x80;
  case k_addr_sysvia | k_via_ORAnh:
    assert(!(p_bbc->sysvia_ACR & 0x01));
    ora = p_bbc->sysvia_ORA;
    ddra = p_bbc->sysvia_DDRA;
    val = ora & ddra;
    val |= (bbc_sysvia_read_porta(p_bbc) & ~ddra);
    return val;
  case k_addr_uservia | k_via_PCR:
    return p_bbc->uservia_PCR;
  case k_addr_uservia | k_via_IFR:
    return 0;
  case k_addr_adc:
    /* No ADC attention needed (bit 6). */
    return 0;
  case k_addr_tube:
    /* Not present -- fall through to return 0xfe. */
    break;
  default:
    assert(0);
  }
  return 0xfe;
}

void
bbc_write_callback(struct bbc_struct* p_bbc, uint16_t addr, unsigned char val) {
  /* We bounce here for ROM writes as well as register writes; ROM writes
   * are simply squashed.
   */
  if (addr < k_registers_offset ||
      addr >= k_registers_offset + k_registers_len) {
    return;
  }

  switch (addr) {
  case k_addr_crtc | k_crtc_address:
    printf("ignoring CRTC address write\n");
    break;
  case k_addr_crtc | k_crtc_data:
    printf("ignoring CRTC data write\n");
    break;
  case k_addr_acia:
    printf("ignoring ACIA write\n");
    break;
  case k_addr_serial_ula:
    printf("ignoring serial ULA write\n");
    break;
  case k_addr_video_ula | k_video_ula_control:
    bbc_set_video_ula_control(p_bbc, val);
    break;
  case k_addr_video_ula | k_video_ula_palette:
    printf("ignoring video ULA palette write\n");
    break;
  case k_addr_rom_latch:
    printf("ignoring ROM latch write\n");
    break;
  case k_addr_sysvia | k_via_ORB:
    assert((p_bbc->sysvia_PCR & 0xa0) != 0x20);
    assert((p_bbc->sysvia_PCR & 0xe0) != 0x80);
    assert((p_bbc->sysvia_PCR & 0xe0) != 0xa0);
    p_bbc->sysvia_ORB = val;
    bbc_sysvia_write_portb(p_bbc);
    break;
  case k_addr_sysvia | k_via_ORA:
    assert((p_bbc->sysvia_PCR & 0x0a) != 0x02);
    assert((p_bbc->sysvia_PCR & 0x0e) != 0x08);
    assert((p_bbc->sysvia_PCR & 0x0e) != 0x0a);
    p_bbc->sysvia_ORA = val;
    bbc_sysvia_write_porta(p_bbc);
    break;
  case k_addr_sysvia | k_via_DDRB:
    p_bbc->sysvia_DDRB = val;
    bbc_sysvia_write_portb(p_bbc);
    break;
  case k_addr_sysvia | k_via_DDRA:
    p_bbc->sysvia_DDRA = val;
    bbc_sysvia_write_porta(p_bbc);
    break;
  case k_addr_sysvia | k_via_T1CH:
    assert(val == 0x27);
    assert((p_bbc->sysvia_ACR & 0xc0) != 0x80);
    p_bbc->sysvia_T1LH = val;
    p_bbc->sysvia_T1CL = p_bbc->sysvia_T1LL;
    p_bbc->sysvia_T1CH = val;
    break;
  case k_addr_sysvia | k_via_T1LL:
    assert(val == 0x0e);
    p_bbc->sysvia_T1LL = val;
    break;
  case k_addr_sysvia | k_via_T1LH:
    assert(val == 0x27);
    /* TODO: clear timer interrupt if acr & 0x40. */
    p_bbc->sysvia_T1LH = val;
    break;
  case k_addr_sysvia | k_via_SR:
    p_bbc->sysvia_SR = val;
    break;
  case k_addr_sysvia | k_via_ACR:
    p_bbc->sysvia_ACR = val;
    printf("new sysvia ACR %x\n", val);
    break;
  case k_addr_sysvia | k_via_PCR:
    assert((val & 0x0e) != 0x0c);
    assert(!(val & 0x08));
    assert((val & 0xe0) != 0xc0);
    assert(!(val & 0x80));
    p_bbc->sysvia_PCR = val;
    printf("new sysvia PCR %x\n", val);
    break;
  case k_addr_sysvia | k_via_IFR:
    p_bbc->sysvia_IFR &= ~(val & 0x7f);
    bbc_check_interrupt(p_bbc);
    break;
  case k_addr_sysvia | k_via_IER:
    if (val & 0x80) {
      p_bbc->sysvia_IER |= (val & 0x7f);
    } else {
      p_bbc->sysvia_IER &= ~(val & 0x7f);
    }
    bbc_check_interrupt(p_bbc);
/*    printf("new sysvia IER %x\n", p_bbc->sysvia_IER);*/
    break;
  case k_addr_sysvia | k_via_ORAnh:
    p_bbc->sysvia_ORA = val;
    bbc_sysvia_write_porta(p_bbc);
    break;
  case k_addr_uservia | k_via_DDRA:
    printf("ignoring user VIA DDRA write\n");
    break;
  case k_addr_uservia | k_via_T1CH:
    printf("ignoring user VIA T1CH write\n");
    break;
  case k_addr_uservia | k_via_T1LL:
    printf("ignoring user VIA T1LL write\n");
    break;
  case k_addr_uservia | k_via_T1LH:
    printf("ignoring user VIA T1LH write\n");
    break;
  case k_addr_uservia | k_via_ACR:
    printf("ignoring user VIA ACR write\n");
    break;
  case k_addr_uservia | k_via_PCR:
    p_bbc->uservia_PCR = val;
    break;
  case k_addr_uservia | k_via_IFR:
    printf("ignoring user VIA IFR write\n");
    break;
  case k_addr_uservia | k_via_IER:
    printf("ignoring user VIA IER write\n");
    break;
  case k_addr_adc:
    printf("ignoring ADC write\n");
    break;
  case k_addr_tube:
    printf("ignoring tube write\n");
    break;
  default:
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
  default:
    printf("warning: unhandled key %d\n", key);
    break;
  }

  *p_row = row;
  *p_col = col;
}

void
bbc_key_pressed(struct bbc_struct* p_bbc, int key) {
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

  /* TODO: we're on the X thread so we have concurrent access issues. */
  bbc_fire_interrupt(p_bbc, 0, k_int_CA2);
}

void
bbc_key_released(struct bbc_struct* p_bbc, int key) {
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
