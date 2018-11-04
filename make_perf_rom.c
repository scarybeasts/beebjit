#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs_6502.h"
#include "emit_6502.h"
#include "util.h"

static const size_t k_rom_size = 16384;

int
main(int argc, const char* argv[]) {
  int fd;
  int arg;
  ssize_t write_ret;
  size_t bytes = 0;
  uint8_t* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) memset(p_mem, '\xf2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;

  /* Copy ROM to RAM. */
  util_buffer_set_pos(p_buf, 0x0000);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_abx, 0xC100);
  emit_STA(p_buf, k_abx, 0x1000);
  emit_DEX(p_buf);
  emit_BNE(p_buf, -9);
  emit_JMP(p_buf, k_abs, 0x1000);

  util_buffer_set_pos(p_buf, 0x0100);
  emit_LDA(p_buf, k_imm, 0x20);
  emit_STA(p_buf, k_zpg, 0x01);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_TAX(p_buf);
  emit_TAY(p_buf);
  emit_STA(p_buf, k_zpg, 0x30);
  emit_STA(p_buf, k_zpg, 0x31);
  for (arg = 1; arg < argc; ++arg) {
    int i;
    if (sscanf(argv[arg], "%x", &i) == 1) {
      util_buffer_add_1b(p_buf, i);
      bytes++;
    }
  }
  emit_INX(p_buf);
  emit_BNE(p_buf, (0xfd - bytes));
  emit_INY(p_buf);
  emit_BNE(p_buf, (0xfa - bytes));
  emit_INC(p_buf, k_zpg, 0x30);
  emit_BNE(p_buf, (0xf6 - bytes));
  emit_INC(p_buf, k_zpg, 0x31);
  emit_BNE(p_buf, (0xf2 - bytes));
  emit_EXIT(p_buf);

  fd = open("perf.rom", O_CREAT | O_WRONLY, 0600);
  if (fd < 0) {
    errx(1, "can't open output rom");
  }
  write_ret = write(fd, p_mem, k_rom_size);
  if (write_ret < 0) {
    errx(1, "can't write output rom");
  }
  if ((size_t) write_ret != k_rom_size) {
    errx(1, "can't write output rom");
  }
  close(fd);

  return 0;
}
