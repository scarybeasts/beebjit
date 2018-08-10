#include "state.h"

#include "bbc.h"

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

struct bem_v2x {
  unsigned char signature[8];
  unsigned char model;
  unsigned char a;
  unsigned char x;
  unsigned char y;
  unsigned char flags;
  unsigned char s;
  uint16_t pc;
  unsigned char nmi;
  unsigned char interrupt;
  uint32_t cycles;
  unsigned char fe30;
  unsigned char fe34;
  unsigned char ram[64 * 1024];
  unsigned char rom[256 * 1024];
} __attribute__((packed));

static const size_t k_snapshot_size = 327885;

void
state_load(struct bbc_struct* p_bbc, const char* p_file_name) {
  struct bem_v2x* p_bem;
  unsigned char snapshot[k_snapshot_size];
  int fd;
  int ret;
  ssize_t read_ret;
  unsigned char* p_mem;

  fd = open(p_file_name, O_RDONLY);
  if (fd < 0) {
    errx(1, "couldn't open state file");
  }

  read_ret = read(fd, snapshot, k_snapshot_size);
  if (read_ret < 0) {
    errx(1, "read failed");
  }
  if (read_ret != k_snapshot_size) {
    errx(1, "wrong snapshot size (expected %zu)", k_snapshot_size);
  }

  ret = close(fd);
  if (ret != 0) {
    errx(1, "close failed");
  }

  p_bem = (struct bem_v2x*) snapshot;
  if (memcmp(p_bem->signature, "BEMSNAP1", 8)) {
    errx(1, "file is not a BEMv2.x snapshot");
  }

  printf("Loading BEMv2.x snapshot, model %u, PC %x\n",
         p_bem->model,
         p_bem->pc);

  p_mem = bbc_get_mem(p_bbc);
  memcpy(p_mem, p_bem->ram, k_bbc_ram_size);

  bbc_set_init_registers(p_bbc,
                         p_bem->a,
                         p_bem->x,
                         p_bem->y,
                         p_bem->s,
                         p_bem->flags,
                         p_bem->pc);

  /* Hack: MODE 7. */
  p_mem[0xfe20] = 0x4b;
}
