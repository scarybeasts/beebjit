#include "bbc.h"

#include "debug.h"
#include "jit.h"

#include <assert.h>
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_guard_size = 4096;
/* TODO: move into jit.h */
static const int k_jit_bytes_per_byte = 256;

static const size_t k_os_rom_offset = 0xc000;
static const size_t k_lang_rom_offset = 0x8000;
static const size_t k_mode7_offset = 0x7c00;
static const size_t k_registers_offset = 0xfc00;
static const size_t k_registers_len = 0x300;
static const size_t k_vector_reset = 0xfffc;
enum {
  k_addr_sysvia = 0xfe40,
};
enum {
  k_via_ORB = 0x0,
  k_via_ORA = 0x1,
  k_via_DDRB = 0x2,
  k_via_DDRA = 0x3,
  k_via_T1CL = 0x4,
  k_via_T1CH = 0x5,
  k_via_T1LL = 0x6,
  k_via_T1LH = 0x7,
  k_via_ACR = 0xb,
  k_via_PCR = 0xc,
  k_via_IER = 0xe,
  k_via_ORAnh = 0xf,
};

struct bbc_struct {
  unsigned char* p_os_rom;
  unsigned char* p_lang_rom;
  int debug_flag;
  int run_flag;
  int print_flag;
  unsigned char* p_map;
  unsigned char* p_mem;
  struct jit_struct* p_jit;
  struct debug_struct* p_debug;
  unsigned char sysvia_ORB;
  unsigned char sysvia_ORA;
  unsigned char sysvia_DDRB;
  unsigned char sysvia_DDRA;
  unsigned char sysvia_T1CL;
  unsigned char sysvia_T1CH;
  unsigned char sysvia_T1LL;
  unsigned char sysvia_T1LH;
  unsigned char sysvia_ACR;
  unsigned char sysvia_PCR;
  unsigned char sysvia_IFR;
  unsigned char sysvia_IER;
  unsigned char sysvia_IC32;
  unsigned char sysvia_sdb;
};

static void*
bbc_jit_thread(void* p) {
  struct bbc_struct* p_bbc = (struct bbc_struct*) p;

  jit_enter(p_bbc->p_jit, k_vector_reset);

  exit(0);
}

struct bbc_struct*
bbc_create(unsigned char* p_os_rom,
           unsigned char* p_lang_rom,
           int debug_flag,
           int run_flag,
           int print_flag) {
  unsigned char* p_map;
  unsigned char* p_mem;
  int ret;
  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }

  p_bbc->p_os_rom = p_os_rom;
  p_bbc->p_lang_rom = p_lang_rom;
  p_bbc->debug_flag = debug_flag;
  p_bbc->run_flag = run_flag;
  p_bbc->print_flag = print_flag;

  p_bbc->sysvia_ORB = 0;
  p_bbc->sysvia_ORA = 0;
  p_bbc->sysvia_DDRB = 0;
  p_bbc->sysvia_DDRA = 0;
  p_bbc->sysvia_T1CL = 0xff;
  p_bbc->sysvia_T1CH = 0xff;
  p_bbc->sysvia_T1LL = 0xff;
  p_bbc->sysvia_T1LH = 0xff;
  p_bbc->sysvia_ACR = 0;
  p_bbc->sysvia_PCR = 0;
  p_bbc->sysvia_IFR = 0;
  p_bbc->sysvia_IER = 0;
  p_bbc->sysvia_IC32 = 0;
  p_bbc->sysvia_sdb = 0;

  p_map = mmap(NULL,
               (k_addr_space_size * (k_jit_bytes_per_byte + 1)) +
                   (k_guard_size * 3),
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS,
               -1,
               0);
  if (p_map == MAP_FAILED) {
    errx(1, "mmap() failed");
  }

  p_bbc->p_map = p_map;
  p_mem = p_map + k_guard_size;
  p_bbc->p_mem = p_mem;

  ret = mprotect(p_map,
                 k_guard_size,
                 PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }
  ret = mprotect(p_mem + k_addr_space_size,
                 k_guard_size,
                 PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }
  ret = mprotect(p_mem + (k_addr_space_size * (k_jit_bytes_per_byte + 1)) +
                     k_guard_size,
                 k_guard_size,
                 PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }

  ret = mprotect(p_mem + k_addr_space_size + k_guard_size,
                 k_addr_space_size * k_jit_bytes_per_byte,
                 PROT_READ | PROT_WRITE | PROT_EXEC);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }

  (void) debug_create(p_bbc->run_flag, p_bbc->print_flag);

  p_bbc->p_jit = jit_create(p_mem,
                            debug_callback,
                            p_bbc,
                            bbc_read_callback,
                            bbc_write_callback);
  if (p_bbc->p_jit == NULL) {
    errx(1, "jit_create failed");
  }

  bbc_reset(p_bbc);

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  int ret;
  jit_destroy(p_bbc->p_jit);
  debug_destroy(p_bbc->p_debug);
  ret = munmap(p_bbc->p_map, (k_addr_space_size * (k_jit_bytes_per_byte + 1)) +
                             (k_guard_size * 3));
  if (ret != 0) {
    errx(1, "munmap failed");
  }
  free(p_bbc);
}

void
bbc_reset(struct bbc_struct* p_bbc) {
  unsigned char* p_mem = p_bbc->p_mem;
  struct jit_struct* p_jit = p_bbc->p_jit;
  int debug_flag = p_bbc->debug_flag;
  /* Clear memory / ROMs. */
  memset(p_mem, '\0', k_addr_space_size);

  /* Copy in OS and language ROM. */
  memcpy(p_mem + k_os_rom_offset, p_bbc->p_os_rom, k_bbc_rom_size);
  memcpy(p_mem + k_lang_rom_offset, p_bbc->p_lang_rom, k_bbc_rom_size);

  /* Initialize hardware registers. */
  memset(p_mem + k_registers_offset, '\0', k_registers_len);

  /* JIT the ROMS. */
  jit_jit(p_jit, k_os_rom_offset, k_bbc_rom_size, debug_flag);
  jit_jit(p_jit, k_lang_rom_offset, k_bbc_rom_size, debug_flag);
}

unsigned char*
bbc_get_mem(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem;
}

unsigned char*
bbc_get_mode7_mem(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem + k_mode7_offset;
}

void
bbc_run_async(struct bbc_struct* p_bbc) {
  pthread_t thread;
  int ret = pthread_create(&thread, NULL, bbc_jit_thread, p_bbc);
  if (ret != 0) {
    errx(1, "couldn't create thread");
  }
}

int
bbc_is_special_read_addr(struct bbc_struct* p_bbc, uint16_t addr) {
  if (addr < 0xfe40 || addr >= 0xfe50) {
    return 0;
  }
  return 1;
}

int
bbc_is_special_write_addr(struct bbc_struct* p_bbc, uint16_t addr) {
  if (addr < 0xfe40 || addr >= 0xfe50) {
    return 0;
  }
  return 1;
}

static void
bbc_sysvia_update_sdb(struct bbc_struct* p_bbc) {
  if (!(p_bbc->sysvia_IC32 & 8)) {
    // Key is not pressed.
    p_bbc->sysvia_sdb &= 0x7f;
  }
}

static unsigned char
bbc_sysvia_read_porta(struct bbc_struct* p_bbc) {
  bbc_sysvia_update_sdb(p_bbc);
  printf("sysvia sdb read %x\n", p_bbc->sysvia_sdb);
  return p_bbc->sysvia_sdb;
}

static void
bbc_sysvia_write_porta(struct bbc_struct* p_bbc) {
  unsigned char via_ora = p_bbc->sysvia_ORA;
  unsigned char via_ddra = p_bbc->sysvia_DDRA;
  unsigned char porta_val = (via_ora & via_ddra) | ~via_ddra;
  unsigned char keyrow = (porta_val >> 4) & 7;
  unsigned char keycol = porta_val & 0xf;
  p_bbc->sysvia_sdb = porta_val;
  printf("sysvia sdb write val %x keyrow %d keycol %d\n",
         porta_val,
         keyrow,
         keycol);
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
  printf("sysvia IC32 orb %x ddrb %x portb %x, new value %x\n",
         via_orb,
         via_ddrb,
         portb_val,
         p_bbc->sysvia_IC32);
}

unsigned char
bbc_read_callback(struct bbc_struct* p_bbc, uint16_t addr) {
  unsigned char val;
  unsigned char ora;
  unsigned char ddra;
  unsigned char orb;
  unsigned char ddrb;
  switch (addr) {
  case k_addr_sysvia | k_via_ORB:
    assert((p_bbc->sysvia_PCR & 0xa0) != 0x20);
    assert(!(p_bbc->sysvia_ACR & 2));
    orb = p_bbc->sysvia_ORB;
    ddrb = p_bbc->sysvia_DDRB;
    val = orb & ddrb;
    /* Read is for joystick and CMOS. 0xff means nothing interesting. */
    val |= (0xff & ~ddrb);
    return val;
  case k_addr_sysvia | k_via_IER:
    return p_bbc->sysvia_IER | 0x80;
  case k_addr_sysvia | k_via_ORAnh:
    assert(!(p_bbc->sysvia_ACR & 1));
    ora = p_bbc->sysvia_ORA;
    ddra = p_bbc->sysvia_DDRA;
    val = ora & ddra;
    val |= (bbc_sysvia_read_porta(p_bbc) & ~ddra);
    return val;
  default:
    assert(0);
  }
  return 0xfe;
}

void
bbc_write_callback(struct bbc_struct* p_bbc, uint16_t addr) {
  unsigned char* p_mem = p_bbc->p_mem;
  unsigned char val = p_mem[addr];

  switch (addr) {
  case k_addr_sysvia | k_via_ORB:
    p_bbc->sysvia_ORB = val;
    bbc_sysvia_write_portb(p_bbc);
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
    assert((p_bbc->sysvia_ACR & 0xc0) != 0x80);
    p_bbc->sysvia_T1LL = val;
    p_bbc->sysvia_T1CL = val;
    p_bbc->sysvia_T1CH = p_bbc->sysvia_T1LH;
    break;
  case k_addr_sysvia | k_via_T1LL:
    p_bbc->sysvia_T1LL = val;
    break;
  case k_addr_sysvia | k_via_T1LH:
    p_bbc->sysvia_T1LH = val;
    break;
  case k_addr_sysvia | k_via_ACR:
    assert(val == 0x60);
    p_bbc->sysvia_ACR = val;
    printf("new sysvia ACR %x\n", val);
    break;
  case k_addr_sysvia | k_via_PCR:
    assert(val == 4);
    p_bbc->sysvia_PCR = val;
    printf("new sysvia PCR %x\n", val);
    break;
  case k_addr_sysvia | k_via_IER:
    if (val & 0x80) {
      p_bbc->sysvia_IER |= (val & 0x7f);
    } else {
      p_bbc->sysvia_IER &= ~(val & 0x7f);
    }
    p_mem[k_addr_sysvia | k_via_IER] = p_bbc->sysvia_IER;
    printf("new sysvia IER %x\n", p_bbc->sysvia_IER);
    break;
  case k_addr_sysvia | k_via_ORAnh:
    p_bbc->sysvia_ORA = val;
    bbc_sysvia_write_porta(p_bbc);
    break;
  default:
    assert(0);
  }
}
