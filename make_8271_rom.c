#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs_6502.h"
#include "emit_6502.h"
#include "test_helper.h"
#include "util.h"

static const size_t k_rom_size = 16384;

static void
set_new_index(struct util_buffer* p_buf, size_t new_index) {
  size_t curr_index = util_buffer_get_pos(p_buf);
  assert(new_index >= curr_index);
  util_buffer_set_pos(p_buf, new_index);
}

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t write_ret;

  uint8_t* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) argc;
  (void) argv;

  (void) memset(p_mem, '\xF2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3FFC] = 0x00;
  p_mem[0x3FFD] = 0xC0;

  /* Check initial status, result, data register operation. */
  set_new_index(p_buf, 0x0000);
  emit_LDA(p_buf, k_abx, 0xFE80);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_abx, 0xFE81);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_abx, 0xFE84);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_abs, 0xFE85);
  emit_LDA(p_buf, k_abx, 0xFE84);
  emit_REQUIRE_EQ(p_buf, 0x41);
  emit_JMP(p_buf, k_abs, 0xC040);

  /* Execute the simplest(?) command, READ_DRIVE_STATUS. */
  set_new_index(p_buf, 0x0040);
  emit_LDA(p_buf, k_imm, 0x2C);
  emit_STA(p_buf, k_abs, 0xFE80);
  /* Wait until done. */
  emit_JSR(p_buf, 0xE000);
  emit_REQUIRE_EQ(p_buf, 0x10);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_REQUIRE_EQ(p_buf, 0x81);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Track status more closely across a multi-parameter but simple command. */
  set_new_index(p_buf, 0x0080);
  /* WRITE_SPECIAL_REGISTER */
  emit_LDA(p_buf, k_imm, 0x3A);
  emit_JSR(p_buf, 0xE040);
  emit_REQUIRE_EQ(p_buf, 0x90);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE080);
  emit_REQUIRE_EQ(p_buf, 0x80);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_JMP(p_buf, k_abs, 0xC0C0);

  /* Set up "mode" register and check status is still correct. */
  set_new_index(p_buf, 0x00C0);
  emit_LDA(p_buf, k_imm, 0x17);
  emit_LDX(p_buf, k_imm, 0xC1);
  /* Write special register. */
  emit_JSR(p_buf, 0xE0C0);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_JMP(p_buf, k_abs, 0xC100);

  /* Exit sequence. */
  set_new_index(p_buf, 0x0100);
  emit_EXIT(p_buf);

  /* Helper functions at $E000+. */
  /* Wait until done. */
  set_new_index(p_buf, 0x2000);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_BMI(p_buf, -5);
  emit_RTS(p_buf);

  /* Write command and wait until accepted. */
  set_new_index(p_buf, 0x2040);
  emit_STA(p_buf, k_abs, 0xFE80);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_TAX(p_buf);
  emit_AND(p_buf, k_imm, 0x40);
  emit_BNE(p_buf, -8);
  emit_TXA(p_buf);
  emit_RTS(p_buf);

  /* Write parameter and wait until accepted. */
  set_new_index(p_buf, 0x2080);
  emit_STA(p_buf, k_abs, 0xFE81);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_TAX(p_buf);
  emit_AND(p_buf, k_imm, 0x20);
  emit_BNE(p_buf, -8);
  emit_TXA(p_buf);
  emit_RTS(p_buf);

  /* Write special register. */
  set_new_index(p_buf, 0x20C0);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STX(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0x3A);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_zpg, 0xF1);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_RTS(p_buf);

  fd = open("8271.rom", O_CREAT | O_WRONLY, 0600);
  if (fd < 0) {
    util_bail("can't open output rom");
  }
  write_ret = write(fd, p_mem, k_rom_size);
  if (write_ret < 0) {
    util_bail("can't write output rom");
  }
  if ((size_t) write_ret != k_rom_size) {
    util_bail("can't write output rom");
  }
  close(fd);

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
