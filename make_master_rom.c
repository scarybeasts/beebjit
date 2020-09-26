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
  /* IRQ vector, @0xFF00 */
  p_mem[0x3FFE] = 0x00;
  p_mem[0x3FFF] = 0xFF;

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
  /* Check HAZEL paging. */
  emit_JSR(p_buf, 0xE000);
  emit_JMP(p_buf, k_abs, 0xC040);

  /* Check LYNNE paging. */
  set_new_index(p_buf, 0x0040);
  emit_JSR(p_buf, 0xE300);
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

  /* Test fixed behavior of JMP (ind) page crossing on 65c12. */
  set_new_index(p_buf, 0x00C0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x10FF);
  emit_LDA(p_buf, k_imm, 0xE1);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0xC1);
  emit_STA(p_buf, k_abs, 0x1100);
  emit_JMP(p_buf, k_ind, 0x10FF);

  set_new_index(p_buf, 0x0100);
  emit_SED(p_buf);
  emit_BRK(p_buf);
  emit_NOP(p_buf);
  emit_JMP(p_buf, k_abs, 0xC140);

  /* Test CMOS read.
   * This sequence used to prod the CMOS is the same as the terminal ROM in
   * MOS3.20, specifically at $8E76.
   */
  set_new_index(p_buf, 0x0140);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* Standard MOS3.20 DDRB value. */
  emit_LDA(p_buf, k_imm, 0xCF);
  emit_STA(p_buf, k_abs, 0xFE42);
  /* Keyboard to column mode. */
  emit_LDA(p_buf, k_imm, 0x03);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* Begin $8E76 sequence, to read CMOS $18. */
  emit_LDX(p_buf, k_imm, 0x18);
  /* Data select low, then address select high. */
  emit_LDA(p_buf, k_imm, 0x02);
  emit_STA(p_buf, k_abs, 0xFE40);
  emit_LDA(p_buf, k_imm, 0x82);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* DDRA to output, and set CMOS address. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE43);
  emit_STX(p_buf, k_abs, 0xFE4F);
  /* CMOS enable, address select still high. */
  emit_LDA(p_buf, k_imm, 0xC2);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* CMOS enable, address select low -> latch address. */
  emit_LDA(p_buf, k_imm, 0x42);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* CMOS enable, read high. */
  emit_LDA(p_buf, k_imm, 0x49);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* DDRA to input. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE43);
  /* CMOS enable, data select high. */
  emit_LDA(p_buf, k_imm, 0x4A);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* Read it!! */
  emit_LDY(p_buf, k_abs, 0xFE4F);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x17);
  /* Check it's still ok with keyboard in autoscan mode. */
  emit_LDA(p_buf, k_imm, 0x4B);
  emit_STA(p_buf, k_abs, 0xFE40);
  emit_LDY(p_buf, k_abs, 0xFE4F);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x17);
  /* CMOS enable, data select low. */
  emit_LDA(p_buf, k_imm, 0x42);
  emit_STA(p_buf, k_abs, 0xFE40);
  /* CMOS disable. */
  emit_LDA(p_buf, k_imm, 0x02);
  emit_STA(p_buf, k_abs, 0xFE40);
  emit_JMP(p_buf, k_abs, 0xC1C0);

  /* Check instruction timings that are different on 65c12. */
  set_new_index(p_buf, 0x01C0);
  emit_CYCLES_RESET(p_buf);
  emit_NOP1(p_buf);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 5);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0xC2);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_CYCLES_RESET(p_buf);
  emit_JMP(p_buf, k_ind, 0x1000);

  /* ROR, ROL, LSR, ASL abx all take 6 cycles if no page crossing on 65c12. */
  set_new_index(p_buf, 0x0200);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 10);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_CYCLES_RESET(p_buf);
  emit_ROR(p_buf, k_abx, 0x1000);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 10);
  emit_CYCLES_RESET(p_buf);
  emit_ROR(p_buf, k_abx, 0x1001);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 11);
  emit_CYCLES_RESET(p_buf);
  emit_INC(p_buf, k_abx, 0x1000);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 11);
  emit_JMP(p_buf, k_abs, 0xC240);

  /* Check floppy cycle stretch region timing, which differs on the Master. */
  set_new_index(p_buf, 0x0240);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abs, 0xFE24);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 8);
  emit_LDX(p_buf, k_abs, 0xFE00);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abs, 0xFE29);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 10);
  emit_JMP(p_buf, k_abs, 0xC280);

  /* Check MOS VDU access to LYNNE (shadow RAM) or main. */
  set_new_index(p_buf, 0x0280);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0x55);
  emit_STA(p_buf, k_abs, 0x4000);
  emit_LDA(p_buf, k_imm, 0x06);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0xAA);
  emit_STA(p_buf, k_abs, 0x4000);
  /* Set ACCCON E bit and access LYNNE because PC=0xCxxx. */
  emit_LDA(p_buf, k_imm, 0x02);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0xFB);
  emit_STA(p_buf, k_abs, 0x4000);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0xFB);
  emit_LDA(p_buf, k_imm, 0x06);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0xFB);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0x55);
  emit_JSR(p_buf, 0xE200);
  emit_JMP(p_buf, k_abs, 0xC300);

  /* Check MOS VDU access to main, perhaps the most surpring combination. */
  set_new_index(p_buf, 0x0300);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0x07);
  emit_STA(p_buf, k_abs, 0x4000);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_abs, 0x4000);
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0x07);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_JMP(p_buf, k_abs, 0xC340);

  /* Exit sequence. */
  set_new_index(p_buf, 0x0340);
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
  emit_RTS(p_buf);

  /* Host a crash gadget at 0xE100. */
  set_new_index(p_buf, 0x2100);
  emit_CRASH(p_buf);

  /* More code that needs to be here because it pages in HAZEL. */
  set_new_index(p_buf, 0x2200);
  /* Bit E with HAZEL paged in and LYNNE paged out. */
  emit_LDA(p_buf, k_imm, 0x0A);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0x55);
  emit_LDA(p_buf, k_imm, 0x97);
  emit_STA(p_buf, k_abs, 0x4000);
  /* Bit E with HAZEL paged in and LYNNE paged in. */
  emit_LDA(p_buf, k_imm, 0x0E);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0xFB);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE34);
  emit_LDA(p_buf, k_abs, 0x4000);
  emit_REQUIRE_EQ(p_buf, 0x97);
  emit_RTS(p_buf);

  /* This needs to be here to avoid triggering MOS VDU access. */
  set_new_index(p_buf, 0x2300);
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
  emit_RTS(p_buf);

  /* IRQ handler at 0xFF00. */
  set_new_index(p_buf, 0x3F00);
  emit_PHP(p_buf);
  emit_PLA(p_buf);
  emit_AND(p_buf, k_imm, 0x08);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_RTI(p_buf);

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
