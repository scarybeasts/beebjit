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

  /* Check ROMSEL / RAMSEL readability and initial state. */
  set_new_index(p_buf, 0x0000);
  emit_LDA(p_buf, k_abs, 0xFE30);
  emit_REQUIRE_EQ(p_buf, 0);
  emit_LDA(p_buf, k_abs, 0xFE34);
  emit_REQUIRE_EQ(p_buf, 0);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE30);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0xFE30);
  emit_REQUIRE_EQ(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE34);
  emit_REQUIRE_EQ(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xE000);

  /* Check LYNNE paging. */
  set_new_index(p_buf, 0x0040);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_abs, 0x3000);
  /* Page in LYNNE. */
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0x42);
  emit_STA(p_buf, k_abs, 0x3000);
  /* Out again. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x3000);
  emit_REQUIRE_EQ(p_buf, 0x41);
  /* In again. */
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x3000);
  emit_REQUIRE_EQ(p_buf, 0x42);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Test write of ROM via page wrap to 0x8000. */
  set_new_index(p_buf, 0x0080);
  emit_LDA(p_buf, k_imm, 0x0F);
  emit_STA(p_buf, k_abs, 0xFE30);
  emit_LDA(p_buf, k_abs, 0x8000);
  emit_STA(p_buf, k_abs, 0x1000);
  /* Set up pointer to $7FFF in zero page. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0xBB);
  /* Write to $7FFF + 1 */
  emit_LDY(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0xF0);
  emit_LDA(p_buf, k_abs, 0x8000);
  emit_CMP(p_buf, k_abs, 0x1000);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC0C0);

  /* Exit sequence. */
  set_new_index(p_buf, 0x00C0);
  emit_LDA(p_buf, k_imm, 0xC2);
  emit_LDX(p_buf, k_imm, 0xC1);
  emit_LDY(p_buf, k_imm, 0xC0);
  emit_EXIT(p_buf);

  /* Host this at $E000 so we can page HAZEL without corrupting our own code. */
  set_new_index(p_buf, 0x2000);
  emit_LDA(p_buf, k_abs, 0xC000);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xC000);
  emit_LDA(p_buf, k_abs, 0xC000);
  emit_CMP(p_buf, k_abs, 0x1000);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Page in HAZEL. */
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xC000);
  emit_LDA(p_buf, k_abs, 0xC000);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  /* Page out HAZEL. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0xC000);
  emit_CMP(p_buf, k_abs, 0x1000);
  emit_REQUIRE_ZF(p_buf, 1);
  /* In again. */
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0xC000);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  /* Out again. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_JMP(p_buf, k_abs, 0xC040);

  fd = open("master.rom", O_CREAT | O_WRONLY, 0600);
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
