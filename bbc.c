#include "bbc.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_os_rom_offset = 0xc000;
static const size_t k_lang_rom_offset = 0x8000;
static const size_t k_mode7_offset = 0x7c00;
static const size_t k_registers_offset = 0xfc00;
static const size_t k_registers_len = 0x300;
static const size_t k_guard_size = 4096;
/* TODO: move into jit.h */
static const int k_jit_bytes_per_byte = 256;

struct bbc_struct {
  unsigned char* p_os_rom;
  unsigned char* p_lang_rom;
  unsigned char* p_map;
  unsigned char* p_mem;
};

struct bbc_struct*
bbc_create(unsigned char* p_os_rom, unsigned char* p_lang_rom) {
  unsigned char* p_map;
  unsigned char* p_mem;
  int ret;
  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }

  p_bbc->p_os_rom = p_os_rom;
  p_bbc->p_lang_rom = p_lang_rom;

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

  bbc_reset(p_bbc);

  return p_bbc;
}

void
bbc_reset(struct bbc_struct* p_bbc) {
  unsigned char* p_mem = p_bbc->p_mem;
  /* Clear memory / ROMs. */
  memset(p_mem, '\0', k_addr_space_size);

  /* Copy in OS and language ROM. */
  memcpy(p_mem + k_os_rom_offset, p_bbc->p_os_rom, k_bbc_rom_size);
  memcpy(p_mem + k_lang_rom_offset, p_bbc->p_lang_rom, k_bbc_rom_size);

  /* Initialize hardware registers. */
  memset(p_mem + k_registers_offset, '\0', k_registers_len);
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  free(p_bbc);
}

unsigned char*
bbc_get_mem(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem;
}

unsigned char*
bbc_get_mode7_mem(struct bbc_struct* p_bbc) {
  return p_bbc->p_mem + k_mode7_offset;
}

int
bbc_is_special_read_addr(struct bbc_struct* p_bbc, uint16_t addr) {
  if (addr < 0xfe00 || addr >= 0xff00) {
    return 0;
  }
  return 1;
}

int
bbc_is_special_write_addr(struct bbc_struct* p_bbc, uint16_t addr) {
  if (addr < 0xfe00 || addr >= 0xff00) {
    return 0;
  }
  return 1;
}

unsigned char
bbc_special_read(struct bbc_struct* p_bbc, uint16_t addr) {
  (void) p_bbc;
  (void) addr;
  return 0xff;
}

void
bbc_special_write(struct bbc_struct* p_bbc, uint16_t addr, unsigned char val) {
  (void) p_bbc;
  (void) addr;
  (void) val;
}
