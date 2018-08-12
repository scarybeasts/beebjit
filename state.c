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
  /* RAM / ROM */
  unsigned char fe30;
  unsigned char fe34;
  unsigned char ram[64 * 1024];
  unsigned char rom[256 * 1024];
  /* System VIA. */
  unsigned char sysvia_ora;
  unsigned char sysvia_orb;
  unsigned char sysvia_ira;
  unsigned char sysvia_irb;
  unsigned char sysvia_unused1;
  unsigned char sysvia_unused2;
  unsigned char sysvia_ddra;
  unsigned char sysvia_ddrb;
  unsigned char sysvia_sr;
  unsigned char sysvia_acr;
  unsigned char sysvia_pcr;
  unsigned char sysvia_ifr;
  unsigned char sysvia_ier;
  unsigned int sysvia_t1l;
  unsigned int sysvia_t2l;
  unsigned int sysvia_t1c;
  unsigned int sysvia_t2c;
  unsigned char sysvia_t1hit;
  unsigned char sysvia_t2hit;
  unsigned char sysvia_ca1;
  unsigned char sysvia_ca2;
  unsigned char sysvia_IC32;
  /* User VIA. */
  unsigned char uservia_ora;
  unsigned char uservia_orb;
  unsigned char uservia_ira;
  unsigned char uservia_irb;
  unsigned char uservia_unused1;
  unsigned char uservia_unused2;
  unsigned char uservia_ddra;
  unsigned char uservia_ddrb;
  unsigned char uservia_sr;
  unsigned char uservia_acr;
  unsigned char uservia_pcr;
  unsigned char uservia_ifr;
  unsigned char uservia_ier;
  unsigned int uservia_t1l;
  unsigned int uservia_t2l;
  unsigned int uservia_t1c;
  unsigned int uservia_t2c;
  unsigned char uservia_t1hit;
  unsigned char uservia_t2hit;
  unsigned char uservia_ca1;
  unsigned char uservia_ca2;
  /* Video ULA. */
  unsigned char ula_control;
  unsigned char ula_palette[16];
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
  if (p_bem->model != 3) {
    errx(1, "can only load standard BBC model snapshots");
  }

  printf("Loading BEMv2.x snapshot, model %u, PC %x\n",
         p_bem->model,
         p_bem->pc);

  if (p_bem->fe30 != 0x0f || p_bem->fe34 != 0x00) {
    errx(1, "can only load standard RAM / ROM setups");
  }

  p_mem = bbc_get_mem(p_bbc);
  memcpy(p_mem, p_bem->ram, k_bbc_ram_size);

  bbc_set_init_registers(p_bbc,
                         p_bem->a,
                         p_bem->x,
                         p_bem->y,
                         p_bem->s,
                         p_bem->flags,
                         p_bem->pc);

  bbc_set_video_ula(p_bbc, p_bem->ula_control);
  bbc_set_sysvia(p_bbc,
                 p_bem->sysvia_ora,
                 p_bem->sysvia_orb,
                 p_bem->sysvia_ddra,
                 p_bem->sysvia_ddrb,
                 p_bem->sysvia_sr,
                 p_bem->sysvia_acr,
                 p_bem->sysvia_pcr,
                 p_bem->sysvia_ifr,
                 p_bem->sysvia_ier,
                 p_bem->sysvia_IC32);
}
