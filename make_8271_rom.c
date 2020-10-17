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

  /* Reset vector: jump to $C000, start of OS ROM. */
  p_mem[0x3FFC] = 0x00;
  p_mem[0x3FFD] = 0xC0;
  /* NMI vector: $0D00 */
  p_mem[0x3FFA] = 0x00;
  p_mem[0x3FFB] = 0x0D;

  /* Check initial status, result, data register operation. */
  set_new_index(p_buf, 0x0000);
  /* Drive 0. */
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_zpg, 0x50);
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

  /* Check return values for READ_SPECIAL_REGISTER. */
  set_new_index(p_buf, 0x0100);
  emit_LDA(p_buf, k_imm, 0x07);
  /* Read special register. */
  emit_JSR(p_buf, 0xE100);
  emit_REQUIRE_EQ(p_buf, 0x10);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_REQUIRE_EQ(p_buf, 0x07);
  emit_JMP(p_buf, k_abs, 0xC140);

  /* The simplest non-simple command: seek to 0 while already at 0. */
  set_new_index(p_buf, 0x0140);
  /* Deselect everything to ensure spindown. */
  emit_LDA(p_buf, k_imm, 0x23);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE0C0);
  /* Install NMI handler. */
  emit_JSR(p_buf, 0xE1C0);
  /* Set spindown count to 2. */
  emit_LDA(p_buf, k_imm, 0x0F);
  emit_LDX(p_buf, k_imm, 0x20);
  emit_JSR(p_buf, 0xE0C0);
  /* Seek to 0. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE140);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_LDA(p_buf, k_abs, 0xFE81);
  /* Not ready. */
  emit_REQUIRE_EQ(p_buf, 0x10);
  emit_LDA(p_buf, k_zpg, 0x51);
  emit_REQUIRE_EQ(p_buf, 0x01);
  /* Check for track 0. */
  emit_JSR(p_buf, 0xE180);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_AND(p_buf, k_imm, 0x02);
  emit_REQUIRE_ZF(p_buf, 0);
  /* Read drive status again, should clear not ready. */
  emit_JSR(p_buf, 0xE180);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_AND(p_buf, k_imm, 0x04);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC1C0);

  /* Format, read IDs, write, read test. */
  set_new_index(p_buf, 0x01C0);
  /* Go ready, seek 0, seek 10. */
  emit_JSR(p_buf, 0xE200);
  emit_LDA(p_buf, k_imm, 0);
  emit_JSR(p_buf, 0xE140);
  emit_LDA(p_buf, k_imm, 10);
  emit_JSR(p_buf, 0xE140);
  /* Set up format buffer, format. */
  emit_LDA(p_buf, k_imm, 10);
  emit_JSR(p_buf, 0xE240);
  emit_LDA(p_buf, k_imm, 10);
  emit_JSR(p_buf, 0xE280);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_TXA(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Read IDs. */
  emit_LDA(p_buf, k_imm, 10);
  emit_JSR(p_buf, 0xE300);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_TXA(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Check the last ID read back ok. */
  emit_LDA(p_buf, k_abs, 0x1024);
  emit_REQUIRE_EQ(p_buf, 0x0A);
  emit_LDA(p_buf, k_abs, 0x1025);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_abs, 0x1026);
  emit_REQUIRE_EQ(p_buf, 0x09);
  emit_LDA(p_buf, k_abs, 0x1027);
  emit_REQUIRE_EQ(p_buf, 0x01);
  /* Write a sector, read it back. */
  emit_LDX(p_buf, k_imm, 0);
  emit_TXA(p_buf);
  emit_STA(p_buf, k_abx, 0x1000);
  emit_INX(p_buf);
  emit_BNE(p_buf, -7);
  /* Write. */
  emit_LDA(p_buf, k_imm, 10);
  emit_LDX(p_buf, k_imm, 2);
  emit_LDY(p_buf, k_imm, 0x21);
  emit_JSR(p_buf, 0xE340);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_TXA(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_STA(p_buf, k_abs, 0x10FF);
  /* Read. */
  emit_LDA(p_buf, k_imm, 10);
  emit_LDX(p_buf, k_imm, 2);
  emit_LDY(p_buf, k_imm, 0x21);
  emit_JSR(p_buf, 0xE380);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_TXA(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0x1001);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_abs, 0x10FF);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_JMP(p_buf, k_abs, 0xC280);

  /* Exit sequence. */
  set_new_index(p_buf, 0x0280);
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

  /* WRITE SPECIAL REGISTER */
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

  /* READ SPECIAL REGISTER */
  set_new_index(p_buf, 0x2100);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x3D);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_RTS(p_buf);

  /* SEEK */
  set_new_index(p_buf, 0x2140);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x29);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  /* TODO: apart from returning the result, this is needed to clear the
   * completion interrupt flag. But on real hardware, I'm not sure I'm seeing
   * completion interrupt for seek. The datasheet says there should be one.
   */
  emit_LDX(p_buf, k_abs, 0xFE81);
  emit_RTS(p_buf);

  /* READ DRIVE STATUS */
  set_new_index(p_buf, 0x2180);
  emit_LDA(p_buf, k_imm, 0x2C);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_JSR(p_buf, 0xE000);
  emit_RTS(p_buf);

  /* Install simple NMI handler. */
  set_new_index(p_buf, 0x21C0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x51);
  /* INC $51, RTI */
  emit_LDA(p_buf, k_imm, 0xE6);
  emit_STA(p_buf, k_abs, 0x0D00);
  emit_LDA(p_buf, k_imm, 0x51);
  emit_STA(p_buf, k_abs, 0x0D01);
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_abs, 0x0D02);
  emit_RTS(p_buf);

  /* Go ready. */
  set_new_index(p_buf, 0x2200);
  /* Select drive 0, load head. */
  emit_LDA(p_buf, k_imm, 0x23);
  emit_LDX(p_buf, k_imm, 0x48);
  emit_JSR(p_buf, 0xE0C0);
  /* Wait for drive ready. */
  emit_JSR(p_buf, 0xE180);
  emit_LDA(p_buf, k_abs, 0xFE81);
  emit_AND(p_buf, k_imm, 0x04);
  emit_BEQ(p_buf, -10);
  emit_RTS(p_buf);

  /* Set up format buffer. */
  set_new_index(p_buf, 0x2240);
  emit_STA(p_buf, k_zpg, 0xF0);
  /* Buffer pointer, 0x60 pointing to 0x1000. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x60);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_zpg, 0x61);
  emit_LDX(p_buf, k_imm, 0);
  emit_LDY(p_buf, k_imm, 0);
  /* Loop here. */
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_idy, 0x60);
  emit_INY(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_idy, 0x60);
  emit_INY(p_buf);
  emit_TXA(p_buf);
  emit_STA(p_buf, k_idy, 0x60);
  emit_INY(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x60);
  emit_INY(p_buf);
  emit_INX(p_buf);
  emit_CPX(p_buf, k_imm, 0x10);
  emit_BNE(p_buf, -24);
  emit_RTS(p_buf);

  /* FORMAT */
  set_new_index(p_buf, 0x2280);
  emit_STA(p_buf, k_zpg, 0xF0);
  /* Reset buffers and NMI handler, for write. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xE2C0);
  /* Format, 5 parameters. Track, GAP3, length / sectors, GAP5, GAP1. */
  emit_LDA(p_buf, k_imm, 0x23);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 21);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 0x2A);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 16);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_LDX(p_buf, k_abs, 0xFE81);
  emit_RTS(p_buf);

  /* Reset buffers and NMI handler. */
  set_new_index(p_buf, 0x22C0);
  /* Save write flag. */
  emit_STA(p_buf, k_zpg, 0x62);
  /* Buffer pointer. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x60);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_zpg, 0x61);
  /* Non-data NMI count. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x63);
  /* $0D00 to JMP $F000. */
  emit_LDA(p_buf, k_imm, 0x4C);
  emit_STA(p_buf, k_abs, 0x0D00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x0D01);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_abs, 0x0D02);
  emit_RTS(p_buf);

  /* READ ID */
  set_new_index(p_buf, 0x2300);
  emit_STA(p_buf, k_zpg, 0xF0);
  /* Reset buffers and NMI handler, for read. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE2C0);
  /* Read ID, 3 parameters. Track, 0, sectors. */
  emit_LDA(p_buf, k_imm, 0x1B);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_imm, 0x0A);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_LDX(p_buf, k_abs, 0xFE81);
  emit_RTS(p_buf);

  /* WRITE DATA */
  set_new_index(p_buf, 0x2340);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STX(p_buf, k_zpg, 0xF1);
  emit_STY(p_buf, k_zpg, 0xF2);
  /* Reset buffers and NMI handler, for write. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xE2C0);
  /* Write data, 3 parameters. Track, sector, length / sectors. */
  emit_LDA(p_buf, k_imm, 0x0B);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_zpg, 0xF1);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_zpg, 0xF2);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_LDX(p_buf, k_abs, 0xFE81);
  emit_RTS(p_buf);

  /* READ DATA */
  set_new_index(p_buf, 0x2380);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STX(p_buf, k_zpg, 0xF1);
  emit_STY(p_buf, k_zpg, 0xF2);
  /* Reset buffers and NMI handler, for read. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xE2C0);
  /* Read data, 3 parameters. Track, sector, length / sectors. */
  emit_LDA(p_buf, k_imm, 0x13);
  emit_ORA(p_buf, k_zpg, 0x50);
  emit_JSR(p_buf, 0xE040);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_zpg, 0xF1);
  emit_JSR(p_buf, 0xE080);
  emit_LDA(p_buf, k_zpg, 0xF2);
  emit_JSR(p_buf, 0xE080);
  emit_JSR(p_buf, 0xE000);
  emit_LDX(p_buf, k_abs, 0xFE81);
  emit_RTS(p_buf);

  /* Jack of all trades NMI handler. */
  set_new_index(p_buf, 0x3000);
  emit_PHA(p_buf);
  emit_TYA(p_buf);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_AND(p_buf, k_imm, 0x04);
  emit_BNE(p_buf, 54);
  /* Non-data NMI. */
  emit_INC(p_buf, k_zpg, 0x63);
  emit_PLA(p_buf);
  emit_TAY(p_buf);
  emit_PLA(p_buf);
  emit_RTI(p_buf);
  set_new_index(p_buf, 0x3040);
  /* Data NMI. */
  emit_LDA(p_buf, k_zpg, 0x62);
  emit_BEQ(p_buf, 60);
  /* Write NMI. */
  emit_LDY(p_buf, k_imm, 0);
  emit_LDA(p_buf, k_idy, 0x60);
  emit_STA(p_buf, k_abs, 0xFE84);
  emit_INC(p_buf, k_zpg, 0x60);
  emit_PLA(p_buf);
  emit_TAY(p_buf);
  emit_PLA(p_buf);
  emit_RTI(p_buf);
  set_new_index(p_buf, 0x3080);
  /* Read NMI. */
  emit_LDA(p_buf, k_abs, 0xFE84);
  emit_LDY(p_buf, k_imm, 0);
  emit_STA(p_buf, k_idy, 0x60);
  emit_INC(p_buf, k_zpg, 0x60);
  emit_PLA(p_buf);
  emit_TAY(p_buf);
  emit_PLA(p_buf);
  emit_RTI(p_buf);

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
  if (0 != close(fd)) {
    util_bail("can't close output file descriptor for rom");
  }

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
