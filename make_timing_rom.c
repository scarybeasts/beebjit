#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs_6502.h"
#include "emit_6502.h"
#include "test_helper.h"
#include "util.h"

static const size_t k_rom_size = 16384;

static size_t
set_new_index(size_t index, size_t new_index) {
  assert(new_index >= index);
  return new_index;
}

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t write_ret;

  size_t index = 0;
  unsigned char* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) argc;
  (void) argv;

  (void) memset(p_mem, '\xf2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3FFC] = 0x00;
  p_mem[0x3FFD] = 0xC0;

  /* Check instruction timings for page crossings in abx mode. */
  util_buffer_set_pos(p_buf, 0x0000);
  emit_CYCLES_RESET(p_buf);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abx, 0x1000); /* LDA abx, no page crossing, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 5);      /* Includes 1 cycle from CYCLES_RESET. */
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abx, 0x10FF); /* LDA abx, page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 6);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abx, 0x1000); /* STA abx, no page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 6);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abx, 0x10FF); /* STA abx, page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 6);
  emit_JMP(p_buf, k_abs, 0xC040);

  /* Check instruction timings for page crossings in idy mode. */
  util_buffer_set_pos(p_buf, 0x0040);
  index = set_new_index(index, 0x40);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xB0);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_abs, 0xB1);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_idy, 0xB0); /* LDA idy, no page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 6);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_idy, 0xB0); /* STA idy, no page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 7);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_idy, 0xB0); /* LDA idy, no page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 7);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_idy, 0xB0); /* STA idy, page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 7);
  emit_JMP(p_buf, k_abs, 0xC080);

  util_buffer_set_pos(p_buf, 0x0080);
  index = set_new_index(index, 0x80);
  emit_LDA(p_buf, k_imm, 0xC2);
  emit_LDX(p_buf, k_imm, 0xC1);
  emit_LDY(p_buf, k_imm, 0xC0);
  emit_EXIT(p_buf);

  fd = open("timing.rom", O_CREAT | O_WRONLY, 0600);
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

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
