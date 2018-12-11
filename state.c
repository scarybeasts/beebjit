#include "state.h"

#include "bbc.h"
#include "sound.h"
#include "state_6502.h"
#include "util.h"
#include "via.h"
#include "video.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

struct bem_v2x {
  uint8_t signature[8];
  uint8_t model;
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t flags;
  uint8_t s;
  uint16_t pc;
  uint8_t nmi;
  uint8_t interrupt;
  uint32_t cycles;
  /* RAM / ROM */
  uint8_t fe30;
  uint8_t fe34;
  uint8_t ram[64 * 1024];
  uint8_t rom[256 * 1024];
  /* System VIA. */
  uint8_t sysvia_ora;
  uint8_t sysvia_orb;
  uint8_t sysvia_ira;
  uint8_t sysvia_irb;
  uint8_t sysvia_unused1;
  uint8_t sysvia_unused2;
  uint8_t sysvia_ddra;
  uint8_t sysvia_ddrb;
  uint8_t sysvia_sr;
  uint8_t sysvia_acr;
  uint8_t sysvia_pcr;
  uint8_t sysvia_ifr;
  uint8_t sysvia_ier;
  int sysvia_t1l;
  int sysvia_t2l;
  int sysvia_t1c;
  int sysvia_t2c;
  uint8_t sysvia_t1hit;
  uint8_t sysvia_t2hit;
  uint8_t sysvia_ca1;
  uint8_t sysvia_ca2;
  uint8_t sysvia_IC32;
  /* User VIA. */
  uint8_t uservia_ora;
  uint8_t uservia_orb;
  uint8_t uservia_ira;
  uint8_t uservia_irb;
  uint8_t uservia_unused1;
  uint8_t uservia_unused2;
  uint8_t uservia_ddra;
  uint8_t uservia_ddrb;
  uint8_t uservia_sr;
  uint8_t uservia_acr;
  uint8_t uservia_pcr;
  uint8_t uservia_ifr;
  uint8_t uservia_ier;
  int uservia_t1l;
  int uservia_t2l;
  int uservia_t1c;
  int uservia_t2c;
  uint8_t uservia_t1hit;
  uint8_t uservia_t2hit;
  uint8_t uservia_ca1;
  uint8_t uservia_ca2;
  /* Video ULA. */
  uint8_t ula_control;
  uint8_t ula_palette[16];
  /* CRTC. */
  uint8_t crtc_regs[18];
  uint8_t crtc_vc;
  uint8_t crtc_sc;
  uint8_t crtc_hc;
  uint8_t crtc_ma_low;
  uint8_t crtc_ma_high;
  uint8_t crtc_maback_low;
  uint8_t crtc_maback_high;
  /* Video. */
  uint8_t video_scrx_low;
  uint8_t video_scrx_high;
  uint8_t video_scry_low;
  uint8_t video_scry_high;
  uint8_t video_oddclock;
  int vidclocks;
  /* Sound: sn76489. */
  uint32_t sn_latch[4];
  uint32_t sn_count[4];
  uint32_t sn_stat[4];
  uint8_t sn_vol[4];
  uint8_t sn_noise;
  uint16_t sn_shift;
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
  if (p_bem->model != 3 && p_bem->model != 4) {
    errx(1, "can only load default BBC model B snapshots, plus sideways RAM");
  }

  printf("Loading BEMv2.x snapshot, model %u, PC %x\n",
         p_bem->model,
         p_bem->pc);
  fflush(stdout);
}

void
state_load(struct bbc_struct* p_bbc, const char* p_file_name) {
  struct bem_v2x* p_bem;
  uint8_t snapshot[k_snapshot_size];
  uint8_t volumes[4];
  uint16_t periods[4];
  uint16_t counters[4];
  int8_t outputs[4];
  uint8_t last_channel;
  uint8_t t1_pb7;
  size_t i;

  struct sound_struct* p_sound = bbc_get_sound(p_bbc);
  struct video_struct* p_video = bbc_get_video(p_bbc);
  struct via_struct* p_system_via = bbc_get_sysvia(p_bbc);
  struct via_struct* p_user_via = bbc_get_uservia(p_bbc);
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);

  state_read(snapshot, p_file_name);

  p_bem = (struct bem_v2x*) snapshot;

  bbc_set_memory_block(p_bbc, 0, k_bbc_ram_size, p_bem->ram);

  for (i = 0; i < k_bbc_num_roms; ++i) {
    bbc_load_rom(p_bbc, i, (p_bem->rom + (i * k_bbc_rom_size)));
  }

  bbc_sideways_select(p_bbc, p_bem->fe30);

  state_6502_set_registers(p_state_6502,
                           p_bem->a,
                           p_bem->x,
                           p_bem->y,
                           p_bem->s,
                           p_bem->flags,
                           p_bem->pc);
  state_6502_set_cycles(p_state_6502, p_bem->cycles);
  if (p_bem->nmi) {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, 1);
  } else {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, 0);
  }
  if (p_bem->interrupt & 1) {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_1, 1);
  } else {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_1, 0);
  }
  if (p_bem->interrupt & 2) {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_2, 1);
  } else {
    state_6502_set_irq_level(p_state_6502, k_state_6502_irq_2, 0);
  }

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
  /* Separate PB7 state isn't saved by b-em so we approximate from ORB. */
  t1_pb7 = !!(p_bem->sysvia_orb & 0x80);
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
                    p_bem->sysvia_t2hit,
                    t1_pb7);
  /* Separate PB7 state isn't saved by b-em so we approximate from ORB. */
  t1_pb7 = !!(p_bem->uservia_orb & 0x80);
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
                    p_bem->uservia_t2hit,
                    t1_pb7);

  /* b-em stores channels in the inverse order: channel 0 is noise. We use
   * channel 3 is noise, matching the raw registers.
   */
  for (i = 0; i < 4; ++i) {
    size_t sn_channel = (3 - i);
    uint16_t period = (p_bem->sn_latch[sn_channel] >> 6);
    uint16_t counter = (p_bem->sn_count[sn_channel] >> 6);
    int8_t output = 1;
    volumes[i] = p_bem->sn_vol[sn_channel];
    /* b-em runs the noise rng twice as fast as we do, so half the timings. */
    if (i == 0) {
      period >>= 1;
      counter >>= 1;
    }
    periods[i] = period;
    counters[i] = counter;
    if (p_bem->sn_stat[sn_channel] >= 16) {
      output = -1;
    }
    outputs[i] = output;
  }

  /* NOTE: b-em doesn't serialize the "last channel updated" state, so it's not
   * possible to fully reconstruct state.
   */
  last_channel = 0;
  sound_set_state(p_sound,
                  &volumes[0],
                  &periods[0],
                  &counters[0],
                  &outputs[0],
                  last_channel,
                  ((p_bem->sn_noise & 0x04) >> 2),
                  (p_bem->sn_noise & 0x03),
                  p_bem->sn_shift);
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
  uint8_t snapshot[k_snapshot_size];
  uint8_t unused_u8;
  uint8_t volumes[4];
  uint16_t periods[4];
  uint16_t counters[4];
  int8_t outputs[4];
  uint8_t last_channel;
  int noise_type;
  uint8_t noise_frequency;
  uint16_t noise_rng;
  size_t i;
  uint8_t t1_pb7;

  struct sound_struct* p_sound = bbc_get_sound(p_bbc);
  struct video_struct* p_video = bbc_get_video(p_bbc);
  struct via_struct* p_system_via = bbc_get_sysvia(p_bbc);
  struct via_struct* p_user_via = bbc_get_uservia(p_bbc);
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  unsigned char* p_mem_read = bbc_get_mem_read(p_bbc);

  (void) memset(snapshot, '\0', k_snapshot_size);

  p_bem = (struct bem_v2x*) snapshot;
  (void) memcpy(p_bem->signature, "BEMSNAP1", 8);
  p_bem->model = 3;

  p_bem->fe30 = bbc_get_romsel(p_bbc);
  p_bem->fe34 = 0x00;

  (void) memcpy(p_bem->ram, p_mem_read, k_bbc_ram_size);

  for (i = 0; i < k_bbc_num_roms; ++i) {
    bbc_save_rom(p_bbc, i, (p_bem->rom + (i * k_bbc_rom_size)));
  }

  state_6502_get_registers(p_state_6502,
                           &p_bem->a,
                           &p_bem->x,
                           &p_bem->y,
                           &p_bem->s,
                           &p_bem->flags,
                           &p_bem->pc);
  /* NOTE: likely integer truncation as the b-em format is only 32-bit. */
  p_bem->cycles = (uint32_t) state_6502_get_cycles(p_state_6502);
  p_bem->interrupt = 0;
  if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_1)) {
    p_bem->interrupt |= 1;
  }
  if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_2)) {
    p_bem->interrupt |= 2;
  }
  p_bem->nmi = 0;
  if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi)) {
    p_bem->nmi = 1;
  }

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
                    &unused_u8,
                    &p_bem->sysvia_IC32,
                    &p_bem->sysvia_t1c,
                    &p_bem->sysvia_t1l,
                    &p_bem->sysvia_t2c,
                    &p_bem->sysvia_t2l,
                    &p_bem->sysvia_t1hit,
                    &p_bem->sysvia_t2hit,
                    &t1_pb7);
  /* PB7 not serialized distinctly so mix it in. */
  if (p_bem->sysvia_acr & 0x80) {
    p_bem->sysvia_orb &= 0x7F;
    p_bem->sysvia_orb |= (t1_pb7 << 7);
  }

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
                    &unused_u8,
                    &unused_u8,
                    &p_bem->uservia_t1c,
                    &p_bem->uservia_t1l,
                    &p_bem->uservia_t2c,
                    &p_bem->uservia_t2l,
                    &p_bem->uservia_t1hit,
                    &p_bem->uservia_t2hit,
                    &t1_pb7);
  /* PB7 not serialized distinctly so mix it in. */
  if (p_bem->uservia_acr & 0x80) {
    p_bem->uservia_orb &= 0x7F;
    p_bem->uservia_orb |= (t1_pb7 << 7);
  }

  /* For now, we measure in 1Mhz ticks and BEM uses 2Mhz ticks. Double up. */
  p_bem->sysvia_t1c <<= 1;
  p_bem->sysvia_t1l <<= 1;
  p_bem->sysvia_t2c <<= 1;
  p_bem->sysvia_t2l <<= 1;
  p_bem->uservia_t1c <<= 1;
  p_bem->uservia_t1l <<= 1;
  p_bem->uservia_t2c <<= 1;
  p_bem->uservia_t2l <<= 1;

  sound_get_state(p_sound,
                  &volumes[0],
                  &periods[0],
                  &counters[0],
                  &outputs[0],
                  &last_channel,
                  &noise_type,
                  &noise_frequency,
                  &noise_rng);
  for (i = 0; i < 4; ++i) {
    size_t sn_channel = (3 - i);
    uint32_t period = periods[i];
    uint32_t counter = counters[i];
    uint32_t stat = 0;
    p_bem->sn_vol[sn_channel] = volumes[i];
    /* b-em runs the noise rng twice as fast as we do, so double the timings. */
    if (i == 0) {
      period <<= 1;
      counter <<= 1;
    }
    p_bem->sn_latch[sn_channel] = (period << 6);
    p_bem->sn_count[sn_channel] = (counter << 6);
    if (outputs[i] == -1) {
      stat = 16;
    }
    p_bem->sn_stat[sn_channel] = stat;
  }
  p_bem->sn_noise = (noise_type << 2);
  p_bem->sn_noise |= noise_frequency;
  p_bem->sn_shift = noise_rng;

  util_file_write(p_file_name, snapshot, k_snapshot_size);
}
