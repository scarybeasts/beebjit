#include "bbc.h"

#include <err.h>
#include <stdlib.h>

struct bbc_struct {
};

struct bbc_struct*
bbc_create() {
  struct bbc_struct* p_bbc = malloc(sizeof(struct bbc_struct));
  if (p_bbc == NULL) {
    errx(1, "couldn't allocate bbc struct");
  }

  return p_bbc;
}

void
bbc_destroy(struct bbc_struct* p_bbc) {
  free(p_bbc);
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
