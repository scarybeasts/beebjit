#include "state.h"

#include "bbc.h"
#include "util.h"
#include "via.h"
#include "video.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

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
  int sysvia_t1l;
  int sysvia_t2l;
  int sysvia_t1c;
  int sysvia_t2c;
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
  int uservia_t1l;
  int uservia_t2l;
  int uservia_t1c;
  int uservia_t2c;
  unsigned char uservia_t1hit;
  unsigned char uservia_t2hit;
  unsigned char uservia_ca1;
  unsigned char uservia_ca2;
  /* Video ULA. */
  unsigned char ula_control;
  unsigned char ula_palette[16];
  /* CRTC. */
  unsigned char crtc_regs[18];
  unsigned char crtc_vc;
  unsigned char crtc_sc;
  unsigned char crtc_hc;
  unsigned char crtc_ma_high;
  unsigned char crtc_ma_low;
  unsigned char crtc_maback_high;
  unsigned char crtc_maback_low;
} __attribute__((packed));

static const size_t k_snapshot_size = 327885;

static void
state_read(unsigned char* p_buf, const char* p_file_name) {
  struct bem_v2x* p_bem;

  size_t len = util_file_read(p_buf, k_snapshot_size, p_file_name);

  if (len != k_snapshot_size) {
    errx(1, "wrong snapshot size (expected %zu)", k_snapshot_size);
  }

  p_bem = (struct bem_v2x*) p_buf;
  if (memcmp(p_bem->signature, "BEMSNAP1", 8)) {
    errx(1, "file is not a BEMv2.x snapshot");
  }
  if (p_bem->model != 3) {
    errx(1, "can only load standard BBC model snapshots");
  }

  printf("Loading BEMv2.x snapshot, model %u, PC %x\n",
         p_bem->model,
         p_bem->pc);
  fflush(stdout);
}

void
state_load(struct bbc_struct* p_bbc, const char* p_file_name) {
  struct bem_v2x* p_bem;
  unsigned char snapshot[k_snapshot_size];

  struct video_struct* p_video = bbc_get_video(p_bbc);
  struct via_struct* p_system_via = bbc_get_sysvia(p_bbc);
  struct via_struct* p_user_via = bbc_get_uservia(p_bbc);

  state_read(snapshot, p_file_name);

  p_bem = (struct bem_v2x*) snapshot;

  bbc_set_memory_block(p_bbc, 0, k_bbc_ram_size, p_bem->ram);

  bbc_set_registers(p_bbc,
                    p_bem->a,
                    p_bem->x,
                    p_bem->y,
                    p_bem->s,
                    p_bem->flags,
                    p_bem->pc);

  video_set_ula_control(p_video, p_bem->ula_control);
  video_set_ula_full_palette(p_video, &p_bem->ula_palette[0]);
  video_set_crtc_registers(p_video, &p_bem->crtc_regs[0]);
  /* For now, we measure in 1Mhz ticks and BEM uses 2Mhz ticks. Divide! */
  /* NOTE: b-em saves int-width negative values outside the expected
   * -2 -> 0xffff range for t2c.
   * Keeps decrementing an int-width negative counter.
   */
  p_bem->sysvia_t1c >>= 1;
  p_bem->sysvia_t1l >>= 1;
  p_bem->sysvia_t2c >>= 1;
  p_bem->sysvia_t2l >>= 1;
  p_bem->uservia_t1c >>= 1;
  p_bem->uservia_t1l >>= 1;
  p_bem->uservia_t2c >>= 1;
  p_bem->uservia_t2l >>= 1;
  via_set_registers(p_system_via,
                    p_bem->sysvia_ora,
                    p_bem->sysvia_orb,
                    p_bem->sysvia_ddra,
                    p_bem->sysvia_ddrb,
                    p_bem->sysvia_sr,
                    p_bem->sysvia_acr,
                    p_bem->sysvia_pcr,
                    p_bem->sysvia_ifr,
                    p_bem->sysvia_ier,
                    0,
                    p_bem->sysvia_IC32,
                    p_bem->sysvia_t1c,
                    p_bem->sysvia_t1l,
                    p_bem->sysvia_t2c,
                    p_bem->sysvia_t2l,
                    p_bem->sysvia_t1hit,
                    p_bem->sysvia_t2hit);
  via_set_registers(p_user_via,
                    p_bem->uservia_ora,
                    p_bem->uservia_orb,
                    p_bem->uservia_ddra,
                    p_bem->uservia_ddrb,
                    p_bem->uservia_sr,
                    p_bem->uservia_acr,
                    p_bem->uservia_pcr,
                    p_bem->uservia_ifr,
                    p_bem->uservia_ier,
                    0,
                    0,
                    p_bem->uservia_t1c,
                    p_bem->uservia_t1l,
                    p_bem->uservia_t2c,
                    p_bem->uservia_t2l,
                    p_bem->uservia_t1hit,
                    p_bem->uservia_t2hit);
}

void
state_load_memory(struct bbc_struct* p_bbc,
                  const char* p_file_name,
                  uint16_t addr,
                  uint16_t len) {
  struct bem_v2x* p_bem;
  unsigned char snapshot[k_snapshot_size];

  assert(((uint16_t)(addr + len)) >= addr);

  state_read(snapshot, p_file_name);

  p_bem = (struct bem_v2x*) snapshot;

  bbc_set_memory_block(p_bbc, addr, len, p_bem->ram + addr);
}

void
state_save(struct bbc_struct* p_bbc, const char* p_file_name) {
  struct bem_v2x* p_bem;
  unsigned char snapshot[k_snapshot_size];
  unsigned char unused_char;

  struct video_struct* p_video = bbc_get_video(p_bbc);
  struct via_struct* p_system_via = bbc_get_sysvia(p_bbc);
  struct via_struct* p_user_via = bbc_get_uservia(p_bbc);
  unsigned char* p_mem_read = bbc_get_mem_read(p_bbc);

  (void) memset(snapshot, '\0', k_snapshot_size);

  p_bem = (struct bem_v2x*) snapshot;
  (void) memcpy(p_bem->signature, "BEMSNAP1", 8);
  p_bem->model = 3;

  p_bem->fe30 = 0x0f;
  p_bem->fe34 = 0x00;

  (void) memcpy(p_bem->ram, p_mem_read, k_bbc_ram_size);

  bbc_get_registers(p_bbc,
                    &p_bem->a,
                    &p_bem->x,
                    &p_bem->y,
                    &p_bem->s,
                    &p_bem->flags,
                    &p_bem->pc);
  p_bem->ula_control = video_get_ula_control(p_video);
  video_get_ula_full_palette(p_video, &p_bem->ula_palette[0]);
  video_get_crtc_registers(p_video, &p_bem->crtc_regs[0]);
  via_get_registers(p_system_via,
                    &p_bem->sysvia_ora,
                    &p_bem->sysvia_orb,
                    &p_bem->sysvia_ddra,
                    &p_bem->sysvia_ddrb,
                    &p_bem->sysvia_sr,
                    &p_bem->sysvia_acr,
                    &p_bem->sysvia_pcr,
                    &p_bem->sysvia_ifr,
                    &p_bem->sysvia_ier,
                    &unused_char,
                    &p_bem->sysvia_IC32,
                    &p_bem->sysvia_t1c,
                    &p_bem->sysvia_t1l,
                    &p_bem->sysvia_t2c,
                    &p_bem->sysvia_t2l,
                    &p_bem->sysvia_t1hit,
                    &p_bem->sysvia_t2hit);
  via_get_registers(p_user_via,
                    &p_bem->uservia_ora,
                    &p_bem->uservia_orb,
                    &p_bem->uservia_ddra,
                    &p_bem->uservia_ddrb,
                    &p_bem->uservia_sr,
                    &p_bem->uservia_acr,
                    &p_bem->uservia_pcr,
                    &p_bem->uservia_ifr,
                    &p_bem->uservia_ier,
                    &unused_char,
                    &unused_char,
                    &p_bem->uservia_t1c,
                    &p_bem->uservia_t1l,
                    &p_bem->uservia_t2c,
                    &p_bem->uservia_t2l,
                    &p_bem->uservia_t1hit,
                    &p_bem->uservia_t2hit);
  /* For now, we measure in 1Mhz ticks and BEM uses 2Mhz ticks. Double up. */
  p_bem->sysvia_t1c <<= 1;
  p_bem->sysvia_t1l <<= 1;
  p_bem->sysvia_t2c <<= 1;
  p_bem->sysvia_t2l <<= 1;
  p_bem->uservia_t1c <<= 1;
  p_bem->uservia_t1l <<= 1;
  p_bem->uservia_t2c <<= 1;
  p_bem->uservia_t2l <<= 1;
  util_file_write(p_file_name, snapshot, k_snapshot_size);
}
