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

  unsigned char* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) argc;
  (void) argv;

  (void) memset(p_mem, '\xF2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* NMI vector. */
  p_mem[0x3FFA] = 0x00;
  p_mem[0x3FFB] = 0xFE;
  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3FFC] = 0x00;
  p_mem[0x3FFD] = 0xC0;
  /* IRQ vector: also jumped to by the BRK instruction. */
  p_mem[0x3FFE] = 0x00;
  p_mem[0x3FFF] = 0xFF;

  /* Check initial 6502 / VIA boot-up status. */
  set_new_index(p_buf, 0x0000);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_imm, 0);      /* Initialize memory we need zeroed. */
  emit_STA(p_buf, k_abs, 0x4041);
  emit_LDA(p_buf, k_abs, 0x01FD);
  emit_REQUIRE_EQ(p_buf, 0x36);   /* 1, BRK, I, Z */
  emit_LDA(p_buf, k_imm, 0xFF);   /* Set all flags upon the PLP. */
  emit_STA(p_buf, k_abs, 0x01FD);
  emit_PLP(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0x01FD);
  emit_PLP(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_CLD(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE62); /* User VIA DDRB. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_abs, 0xFE6B); /* User VIA ACR to PB7 mode. */
  emit_LDA(p_buf, k_abs, 0xFE60); /* User VIA ORB. */
  emit_REQUIRE_EQ(p_buf, 0xFF);   /* PB7 initial state should be 1. */
  emit_JMP(p_buf, k_abs, 0xC050);

  /* Check TSX / TXS stack setup. */
  set_new_index(p_buf, 0x0050);
  emit_TSX(p_buf);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_TXS(p_buf);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Check CMP vs. flags. */
  set_new_index(p_buf, 0x0080);
  emit_LDX(p_buf, k_imm, 0xEE);
  emit_CPX(p_buf, k_imm, 0xEE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 0);
  emit_CPX(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_CPX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC0C0);

  /* Some ADC tests. */
  set_new_index(p_buf, 0x00C0);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_imm, 0x03);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7F);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7F);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC100);

  /* Some SBC tests. */
  set_new_index(p_buf, 0x0100);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 0);
  emit_SBC(p_buf, k_imm, 0x80);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_SBC(p_buf, k_imm, 0x7F);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC150);

  /* Some ROR / ROL tests. */
  set_new_index(p_buf, 0x0150);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_acc, 0);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x80);
  emit_ROR(p_buf, k_acc, 0);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_EQ(p_buf, 0xC0);
  emit_SEC(p_buf);
  emit_ROL(p_buf, k_acc, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x81);
  emit_JMP(p_buf, k_abs, 0xC190);

  /* Test BRK! */
  set_new_index(p_buf, 0x0190);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_STX(p_buf, k_zpg, 0x00);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_TXS(p_buf);
  emit_CLI(p_buf);
  emit_BRK(p_buf);                /* Calls vector $FFFE -> $FF00 (RTI) */
  emit_CRASH(p_buf);              /* Jumped over by RTI. */
  emit_LDX(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_PHP(p_buf);                /* Check I flag state was preserved. */
  emit_PLA(p_buf);
  emit_AND(p_buf, k_imm, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC1C0);

  /* Test shift / rotate instuction coalescing. */
  set_new_index(p_buf, 0x01C0);
  emit_LDA(p_buf, k_imm, 0x05);
  emit_ASL(p_buf, k_acc, 0);
  emit_ASL(p_buf, k_acc, 0);
  emit_CMP(p_buf, k_imm, 0x14);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_acc, 0);
  emit_ROR(p_buf, k_acc, 0);
  emit_ROR(p_buf, k_acc, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_CMP(p_buf, k_imm, 0x22);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC200);

  /* Test indexed zero page addressing. */
  set_new_index(p_buf, 0x0200);
  emit_LDX(p_buf, k_imm, 0xFD);
  emit_LDY(p_buf, k_imm, 0x03);
  emit_STX(p_buf, k_zpy, 0x04);
  emit_LDY(p_buf, k_imm, 0xFF);
  emit_LDX(p_buf, k_imm, 0x05);
  emit_STY(p_buf, k_zpx, 0x03);
  emit_LDX(p_buf, k_imm, 0x02);
  emit_LDA(p_buf, k_zpx, 0x05);
  emit_STA(p_buf, k_zpx, 0x07);
  emit_LDA(p_buf, k_zpx, 0x07);
  emit_CMP(p_buf, k_imm, 0xFD);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0xD1);
  emit_LDA(p_buf, k_zpx, 0x37);   /* Zero page wrap. */ /* Addr: $08 */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDY(p_buf, k_imm, 0xD1);
  emit_LDX(p_buf, k_zpy, 0x37);   /* Zero page wrap. */ /* Addr: $08 */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC240);

  /* Test indirect indexed zero page addressing. */
  set_new_index(p_buf, 0x0240);
  emit_LDX(p_buf, k_imm, 0xD1);
  emit_LDA(p_buf, k_idx, 0x36);   /* Zero page wrap. */ /* Addr: $07 -> $FFFD */
  emit_CMP(p_buf, k_imm, 0xC0);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC280);

  /* Test simple JSR / RTS pair. */
  set_new_index(p_buf, 0x0280);
  emit_JSR(p_buf, 0xC286);
  emit_JMP(p_buf, k_abs, 0xC2C0);
  emit_RTS(p_buf);

  /* Test BIT. */
  set_new_index(p_buf, 0x02C0);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_INX(p_buf);
  emit_BIT(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_BIT(p_buf, k_abs, 0x0000);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC300);

  /* Test RTI. */
  set_new_index(p_buf, 0x0300);
  emit_LDA(p_buf, k_imm, 0xC3);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x40);
  emit_PHA(p_buf);
  emit_PHP(p_buf);
  emit_RTI(p_buf);

  /* Test most simple self-modifying code. */
  set_new_index(p_buf, 0x0340);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x2000);
  emit_JSR(p_buf, 0x2000);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_abs, 0x2000);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x2001);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_JSR(p_buf, 0x2000);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC380);

  /* Test self-modifying an operand of an opcode. */
  set_new_index(p_buf, 0x0380);
  /* Stores LDA #$00; RTS at $1000. */
  emit_LDA(p_buf, k_imm, 0xA9);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x1002);
  emit_JSR(p_buf, 0x1000);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Modify LDA #$00 at $1000 to be LDA #$01. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_JSR(p_buf, 0x1000);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC3C0);

  /* Copy some ROM to RAM so we can test self-modifying code easier.
   * This copy uses indirect Y addressing which wasn't actually previously
   * tested either.
   */
  set_new_index(p_buf, 0x03C0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF2);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_STA(p_buf, k_zpg, 0xF3);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_STA(p_buf, k_idy, 0xF2);
  emit_INY(p_buf);
  emit_BNE(p_buf, -7);
  emit_INC(p_buf, k_zpg, 0xF1);
  emit_INC(p_buf, k_zpg, 0xF3);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_STA(p_buf, k_idy, 0xF2);
  emit_INY(p_buf);
  emit_BNE(p_buf, -7);
  emit_JMP(p_buf, k_abs, 0xC400);

  /* Test some more involved self-modifying code situations. */
  set_new_index(p_buf, 0x0400);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0, INX. */
  emit_JSR(p_buf, 0x3002);        /* INX */
  emit_CPX(p_buf, k_imm, 0x02);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Flip INX to DEX at $3002. */
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_abs, 0x3002);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0, DEX. */
  emit_REQUIRE_NF(p_buf, 1);
  /* Flip LDX #$00 to LDX #$60 at $3000. */
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x3001);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0x60, DEX. */
  emit_REQUIRE_NF(p_buf, 0);
  /* The horrors: jump into the middle of an instruction. */
  emit_JSR(p_buf, 0x3001);        /* 0x60 == RTS */
  emit_JSR(p_buf, 0x3000);
  emit_JMP(p_buf, k_abs, 0xC440);

  /* Tests a real self-modifying copy loop. */
  set_new_index(p_buf, 0x0440);
  emit_LDA(p_buf, k_imm, 0xE1);
  emit_STA(p_buf, k_abs, 0x1CCC);
  emit_JSR(p_buf, 0x3010);
  emit_LDA(p_buf, k_abs, 0x0CCC);
  emit_CMP(p_buf, k_imm, 0xE1);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC480);

  /* Tests a sequence of forwards / backwards jumps that confused the JIT
   * address tracking.
   */
  set_new_index(p_buf, 0x0480);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_JMP(p_buf, k_abs, 0xC488); /* L1 */
  emit_INX(p_buf);
  emit_BNE(p_buf, -3);            /* L1 here. */
  emit_DEY(p_buf);
  emit_BEQ(p_buf, -5);
  emit_JMP(p_buf, k_abs, 0xC4C0);

  /* Test self-modifying within a block. */
  set_new_index(p_buf, 0x04C0);
  emit_JSR(p_buf, 0x3030);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC500);

  /* Test self-modifying code that may invalidate assumptions about instruction
   * flag optimizations.
   */
  set_new_index(p_buf, 0x0500);
  emit_JSR(p_buf, 0x3040);
  emit_LDA(p_buf, k_imm, 0x60);
  /* Store RTS at $3042. */
  emit_STA(p_buf, k_abs, 0x3042);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0x3040);
  /* Test to see if the flags update went missing due to the self modifying
   * code changing flag expectations within the block.
   */
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC540);

  /* Test various simple hardware register read / writes and initial state. */
  set_new_index(p_buf, 0x0540);
  emit_LDA(p_buf, k_abs, 0xFE42); /* DDRB should initialize to 0, all inputs. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE40); /* Inputs should be all 1. */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE42); /* Set DDRB to all outputs. */
  emit_LDA(p_buf, k_abs, 0xFE40); /* Outputs should be all 0. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_abs, 0xFE4A);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0x41);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0xCA);
  emit_LDA(p_buf, k_abx, 0xFD80);
  emit_CMP(p_buf, k_imm, 0x41);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0x0A);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_abx, 0xFE40);
  emit_REQUIRE_CF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0xA0);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_DEC(p_buf, k_abs, 0xFE4A);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0x9F);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC5A0);

  /* Test writing to ROM memory. */
  set_new_index(p_buf, 0x05A0);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0x39);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_idy, 0x02);   /* $C039 */
  emit_STA(p_buf, k_zpg, 0x04);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x02);   /* $C039 */
  emit_LDA(p_buf, k_idy, 0x02);
  emit_CMP(p_buf, k_zpg, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_STA(p_buf, k_idx, 0x02);   /* $C039 */
  emit_LDA(p_buf, k_imm, 0x1B);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_idy, 0x02);   /* $801B */
  emit_STA(p_buf, k_zpg, 0x04);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x02);   /* $801B */
  emit_LDA(p_buf, k_idy, 0x02);
  emit_CMP(p_buf, k_zpg, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC5E0);

  /* Test LDX with aby addressing, which was broken, oops! */
  set_new_index(p_buf, 0x05E0);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x04);
  emit_LDX(p_buf, k_aby, 0xC5E0);
  emit_CPX(p_buf, k_imm, 0xBE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC600);

  /* Test a variety of additional high-address reads and writes of interest. */
  set_new_index(p_buf, 0x0600);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_abx, 0xFBFF);
  emit_CMP(p_buf, k_imm, 0x7D);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_STA(p_buf, k_abs, 0x801B);
  emit_LDA(p_buf, k_abs, 0xC603);
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_DEC(p_buf, k_abs, 0xC603);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_CLC(p_buf);
  emit_ROR(p_buf, k_abs, 0xC603);
  emit_REQUIRE_CF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_idy, 0x02);   /* $FFFF */
  emit_STA(p_buf, k_idx, 0x02);   /* $FFFF */
  emit_STA(p_buf, k_abs, 0xFFFF);
  emit_STA(p_buf, k_abx, 0xFFFF);
  emit_STA(p_buf, k_aby, 0xFFFF);
  emit_LDA(p_buf, k_abs, 0xFFFF);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_LDA(p_buf, k_abx, 0xFFFF);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_LDA(p_buf, k_aby, 0xFFFF);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_JMP(p_buf, k_abs, 0xC670);

  /* Test an interesting bug we had with self-modifying code where two
   * adjacent instructions are clobbered.
   */
  set_new_index(p_buf, 0x0670);
  emit_NOP(p_buf);
  emit_JSR(p_buf, 0x3050);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x3050);
  emit_STA(p_buf, k_abs, 0x3051);
  emit_JSR(p_buf, 0x3050);
  emit_JMP(p_buf, k_abs, 0xC690);

  /* Test JIT invalidation through different write modes. */
  set_new_index(p_buf, 0x0690);
  emit_JSR(p_buf, 0x3050);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_LDY(p_buf, k_imm, 0x02);
  emit_LDA(p_buf, k_imm, 0xCA);   /* DEX */
  emit_STA(p_buf, k_abx, 0x304F);
  emit_JSR(p_buf, 0x3050);
  emit_CPX(p_buf, k_imm, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x88);   /* DEY */
  emit_STA(p_buf, k_aby, 0x304E);
  emit_JSR(p_buf, 0x3050);
  emit_CPY(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC6C0);

  /* Test JIT invalidation through remaining write modes. */
  set_new_index(p_buf, 0x06C0);
  emit_LDA(p_buf, k_imm, 0xEA);   /* NOP */
  emit_STA(p_buf, k_abs, 0x3050);
  emit_JSR(p_buf, 0x3050);
  emit_LDA(p_buf, k_imm, 0x48);
  emit_STA(p_buf, k_zpg, 0x8F);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_STA(p_buf, k_zpg, 0x90);
  emit_LDY(p_buf, k_imm, 0x08);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_idy, 0x8F);
  emit_JSR(p_buf, 0x3050);
  emit_CPX(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x50);
  emit_STA(p_buf, k_zpg, 0x8F);
  emit_LDX(p_buf, k_imm, 0x10);
  emit_LDA(p_buf, k_imm, 0xC8);   /* INY */
  emit_STA(p_buf, k_idx, 0x7F);
  emit_JSR(p_buf, 0x3050);
  emit_CPY(p_buf, k_imm, 0x09);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC700);

  /* Test JIT recompilation bug leading to corrupted code generation.
   * Trigger condition is replacing an opcode resulting in short generated code
   * with one resulting in longer generated code.
   */
  set_new_index(p_buf, 0x0700);
  emit_JSR(p_buf, 0x3070);
  emit_LDA(p_buf, k_imm, 0x48);   /* PHA */
  emit_STA(p_buf, k_abs, 0x3070);
  emit_LDA(p_buf, k_imm, 0x68);   /* PLA */
  emit_STA(p_buf, k_abs, 0x3071);
  emit_JSR(p_buf, 0x3070);
  emit_JMP(p_buf, k_abs, 0xC740);

  /* Test a few simple VIA behaviors. */
  set_new_index(p_buf, 0x0740);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE60); /* User VIA ORB */
  emit_STA(p_buf, k_abs, 0xFE62); /* User VIA DDRB */
  emit_LDA(p_buf, k_abs, 0xFE60); /* User VIA ORB */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE70); /* User VIA ORB */ /* Alt address */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE62); /* User VIA DDRB */
  emit_LDA(p_buf, k_abs, 0xFE60); /* User VIA ORB */
  emit_CMP(p_buf, k_imm, 0xFE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE60); /* User VIA ORB */
  emit_LDA(p_buf, k_abs, 0xFE60); /* User VIA ORB */
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC780);

  /* Test the firing of a timer interrupt. */
  set_new_index(p_buf, 0x0780);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE4E); /* sysvia IER */
  emit_CMP(p_buf, k_imm, 0x80);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x0E);
  emit_STA(p_buf, k_abs, 0xFE44); /* sysvia T1CL */
  emit_LDA(p_buf, k_imm, 0x27);
  emit_STA(p_buf, k_abs, 0xFE45); /* sysvia T1CH */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* sysvia IFR */
  emit_AND(p_buf, k_imm, 0x40);   /* TIMER1 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0xC0);   /* set, TIMER1 */
  emit_STA(p_buf, k_abs, 0xFE4E); /* sysvia IER */
  emit_CLI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_BEQ(p_buf, -4);            /* Wait until an interrupt is serviced. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* sysvia IFR */
  emit_AND(p_buf, k_imm, 0x40);   /* TIMER1 */
  emit_REQUIRE_ZF(p_buf, 0);
  emit_LDA(p_buf, k_imm, 0x27);
  emit_STA(p_buf, k_abs, 0xFE45); /* sysvia T1CH */ /* Clears TIMER1. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* sysvia IFR */
  emit_AND(p_buf, k_imm, 0x40);   /* TIMER1 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_CLV(p_buf);
  /* Loop again, starting here, with interrupts disabled.
   * Interrupt enable occurs briefly and within the block. Interrupt must still
   * fire :)
   */
  emit_CLI(p_buf);
  emit_BVC(p_buf,
           k_emit_crash_len);     /* Skip CRASH to test RTI PC miss bug. */
  emit_CRASH(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_CMP(p_buf, k_imm, 0x02);
  emit_BNE(p_buf, -11);
  emit_LDA(p_buf, k_abs, 0xFE44); /* sysvia T1CL */ /* Clears TIMER1. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* sysvia IFR */
  emit_AND(p_buf, k_imm, 0x40);   /* TIMER1 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x40);   /* clear, TIMER1 */
  emit_STA(p_buf, k_abs, 0xFE4E); /* sysvia IER */
  emit_LDA(p_buf, k_zpg, 0x01);   /* Check BRK flag wasn't set in interrupt. */
  emit_AND(p_buf, k_imm, 0x10);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC800);

  /* Test the firing of a timer interrupt -- to be contrarian, let's now test
   * TIMER2 on the user VIA, using alternative registers.
   */
  set_new_index(p_buf, 0x0800);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_imm, 0x0E);
  emit_STA(p_buf, k_abs, 0xFE78); /* uservia T2CL */
  emit_LDA(p_buf, k_imm, 0x27);
  emit_STA(p_buf, k_abs, 0xFE79); /* uservia T2CH */
  emit_LDA(p_buf, k_abs, 0xFE7D); /* uservia IFR */
  emit_AND(p_buf, k_imm, 0x20);   /* TIMER2 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0xA0);   /* set, TIMER2 */
  emit_STA(p_buf, k_abs, 0xFE7E); /* uservia IER */
  emit_CLI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_BEQ(p_buf, -4);            /* Wait until an interrupt is serviced. */
  emit_LDA(p_buf, k_abs, 0xFE7D); /* uservia IFR */
  emit_AND(p_buf, k_imm, 0x20);   /* TIMER2 */
  emit_REQUIRE_ZF(p_buf, 0);
  emit_LDA(p_buf, k_imm, 0x27);
  emit_STA(p_buf, k_abs, 0xFE79); /* uservia T2CH */ /* Clears TIMER2. */
  emit_LDA(p_buf, k_abs, 0xFE7D); /* uservia IFR */
  emit_AND(p_buf, k_imm, 0x20);   /* TIMER2 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x20);   /* clear, TIMER2 */
  emit_STA(p_buf, k_abs, 0xFE7E); /* uservia IER */
  emit_JMP(p_buf, k_abs, 0xC850);

  /* Test dynamic operands for a branch instruction, and a no-operands
   * instruction.
    */
  set_new_index(p_buf, 0x0850);
  emit_JSR(p_buf, 0x3080);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0x3083);
  emit_JSR(p_buf, 0x3080);
  emit_CPX(p_buf, k_imm, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x3083);
  emit_JSR(p_buf, 0x3080);
  emit_CPX(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_abs, 0x3084);
  emit_JSR(p_buf, 0x3080);
  emit_JMP(p_buf, k_abs, 0xC880);

  /* Tests for a bug where the NZ flags update was going missing if there
   * were a couple of STA instructions in a row.
   */
  set_new_index(p_buf, 0x0880);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_INX(p_buf);
  /* ZF is now 1. This should clear ZF. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC8C0);

  /* Used to test that timers don't return the same value twice in a row, but
   * that test is now only valid in accurate mode.
   */
  set_new_index(p_buf, 0x08C0);
  emit_JMP(p_buf, k_abs, 0xC900);

  /* Test that the carry flag optimizations don't break anything. */
  set_new_index(p_buf, 0x0900);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x00);
  emit_CMP(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC940);

  /* Tests for a bug where the NZ flags were updated from the wrong register. */
  set_new_index(p_buf, 0x0940);
  emit_JSR(p_buf, 0xC965);      /* Create block boundary at the RTS. */
  emit_JSR(p_buf, 0xC960);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC980);
  util_buffer_set_pos(p_buf, 0x0960);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_TAX(p_buf);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_RTS(p_buf);

  /* Give the carry flag tracking logic a good workout. */
  set_new_index(p_buf, 0x0980);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_LSR(p_buf, k_acc, 0);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_TAX(p_buf);
  emit_INX(p_buf);
  emit_ROL(p_buf, k_acc, 0);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_ROR(p_buf, k_acc, 0);
  emit_TAY(p_buf);
  emit_CPX(p_buf, k_imm, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_CPY(p_buf, k_imm, 0x82);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC9C0);

  /* Give the overflow flag tracking logic a good workout. */
  set_new_index(p_buf, 0x09C0);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x7F);   /* Sets OF=1 */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LSR(p_buf, k_acc, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_CLV(p_buf);
  emit_REQUIRE_OF(p_buf, 0);
  emit_SEC(p_buf);
  emit_ADC(p_buf, k_imm, 0x7F);   /* Sets OF=1 */
  emit_REQUIRE_OF(p_buf, 1);
  emit_CMP(p_buf, k_imm, 0xFF);   /* Should not affect OF. */
  emit_REQUIRE_OF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x00);   /* Sets OF=0 */
  emit_LDA(p_buf, k_imm, 0x80);
  emit_ROR(p_buf, k_acc, 0);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xCA00);

  set_new_index(p_buf, 0x0A00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x7F01);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0x42);
  emit_STA(p_buf, k_abx, 0x7F01);
  emit_CMP(p_buf, k_abs, 0x7F01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCA80);

  /* Gap here for re-use. */

  /* Test that paging ROMs invalidates any previous artifcats, e.g. JIT
   * compilation.
   */
  set_new_index(p_buf, 0x0A80);
  emit_LDA(p_buf, k_imm, 0x08);   /* 0x8 is an alias for 0xC where BASIC is. */
  emit_STA(p_buf, k_abs, 0xFE30); /* Page in BASIC. */
  emit_JSR(p_buf, 0x8000);        /* A != 1 so this just RTS's. */
  emit_LDA(p_buf, k_imm, 0x0D);
  emit_STA(p_buf, k_abs, 0xFE34); /* Page in DFS. FE34 is also ROMSEL. */
  emit_LDA(p_buf, k_imm, 0xA0);   /* Set up the BRK vector to $CAA0. */
  emit_STA(p_buf, k_abs, 0x4040);
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_abs, 0x4041);
  emit_JSR(p_buf, 0x8000);        /* Should hit BRK. */
  emit_CRASH(p_buf);
  util_buffer_set_pos(p_buf, 0x0AA0);
  emit_LDA(p_buf, k_imm, 0x0C);
  emit_STA(p_buf, k_abs, 0xFE30); /* Page in BASIC. */
  emit_JSR(p_buf, 0x8002);        /* ZF == 0 so this just RTS's. */
  emit_LDA(p_buf, k_imm, 0x0D);
  emit_STA(p_buf, k_abs, 0xFE30); /* Page in DFS. */
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_abs, 0x4040);
  emit_JSR(p_buf, 0x8002);        /* Another BRK. */
  emit_CRASH(p_buf);
  util_buffer_set_pos(p_buf, 0x0AC0);
  emit_LDA(p_buf, k_imm, 0x00);   /* Unset BRK vector. */
  emit_STA(p_buf, k_abs, 0x4040);
  emit_STA(p_buf, k_abs, 0x4041);
  emit_JMP(p_buf, k_abs, 0xCAE0);

  /* Tests triggering a simple NMI. */
  set_new_index(p_buf, 0x0AE0);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFEE3);  /* Test mapping: raise NMI. */
  emit_TAY(p_buf);
  emit_BEQ(p_buf, -3);
  emit_JMP(p_buf, k_abs, 0xCB00);

  /* Tests a bug where SEI clobbered the carry flag in JIT mode. */
  set_new_index(p_buf, 0x0B00);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_ADC(p_buf, k_imm, 0x00);
  emit_SEI(p_buf);
  emit_SEI(p_buf);
  emit_ADC(p_buf, k_imm, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_CLI(p_buf);
  emit_JMP(p_buf, k_abs, 0xCB40);

  /* Test a mixed bag of opcodes not otherwise covered and unearthed when
   * adding inturbo mode.
   */
  set_new_index(p_buf, 0x0B40);
  emit_LDX(p_buf, k_imm, 0xAA);
  emit_TXA(p_buf);
  emit_CMP(p_buf, k_imm, 0xAA);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDY(p_buf, k_imm, 0xBB);
  emit_TYA(p_buf);
  emit_CMP(p_buf, k_imm, 0xBB);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_EOR(p_buf, k_imm, 0x41);
  emit_CMP(p_buf, k_imm, 0xFA);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_STY(p_buf, k_zpg, 0xFF);
  emit_LDA(p_buf, k_zpg, 0xFF);
  emit_CMP(p_buf, k_imm, 0xBB);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_abs, 0x7000);
  emit_LDA(p_buf, k_imm, 0xCB);
  emit_STA(p_buf, k_abs, 0x7001);
  emit_JMP(p_buf, k_ind, 0x7000);
  emit_CRASH(p_buf);

  /* Tests for a bug in inturbo mode where the upper bits of the carry flag
   * register got corrupted in inturbo mode.
   */
  set_new_index(p_buf, 0x0B80);
  emit_LDA(p_buf, k_imm, 0x07);
  emit_CLC(p_buf);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_CLC(p_buf);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_imm, 0x03);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCBC0);

  /* Test VIA PB7 handling works correctly. */
  set_new_index(p_buf, 0x0BC0);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE6E); /* IER: turn off interrupts. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE62); /* DDRB: all outputs. */
  emit_STA(p_buf, k_abs, 0xFE60); /* ORB: 0xFF */
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_abs, 0xFE6B); /* ACR: PB7 output mode, continuous. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE6C); /* PCR: set to known value. */
  emit_STA(p_buf, k_abs, 0xFE64); /* T1CL: 0 */
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_abs, 0xFE65); /* T1CH: 0x10 */
  emit_LDA(p_buf, k_abs, 0xFE60);
  emit_CMP(p_buf, k_imm, 0x7F);   /* ORB: needs PB7 low. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE62); /* DDRB: all inputs. */
  emit_LDA(p_buf, k_abs, 0xFE60);
  emit_CMP(p_buf, k_imm, 0x7F);   /* IRB: needs PB7 low. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE60);
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_BNE(p_buf, -7);            /* Loop until PB7 flips. */
  emit_LDA(p_buf, k_abs, 0xFE60);
  emit_CMP(p_buf, k_imm, 0x7F);
  emit_BNE(p_buf, -7);            /* Loop until PB7 flips back. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE6B); /* ACR: not PB7 output mode, one shot. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE62); /* DDRB: all outputs. */
  emit_LDA(p_buf, k_abs, 0xFE60);
  emit_CMP(p_buf, k_imm, 0xFF);   /* Make sure low PB7 doesn't trash ORB. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCC40);

  /* Test a few undocumented opcodes used by games. */
  set_new_index(p_buf, 0x0C40);
  /* NOP zp, used by Pipeline and Citadel. */
  util_buffer_add_2b(p_buf, 0x04, 0x00);
  /* NOP zpx, used by Pipeline. */
  util_buffer_add_2b(p_buf, 0xF4, 0x00);
  /* NOP abx, used by Zalaga. */
  util_buffer_add_3b(p_buf, 0xDC, 0x00, 0x00);
  /* SAX zp, used by Zalaga. */
  emit_LDA(p_buf, k_imm, 0xA9);
  emit_LDX(p_buf, k_imm, 0x34);
  util_buffer_add_2b(p_buf, 0x87, 0x00);
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_REQUIRE_EQ(p_buf, 0x20);
  /* ALR imm, used by Zalaga. */
  emit_LDA(p_buf, k_imm, 0xA9);
  util_buffer_add_2b(p_buf, 0x4B, 0x34);
  emit_REQUIRE_EQ(p_buf, 0x10);
  /* SLO zp, used by Zalaga. */
  emit_LDA(p_buf, k_imm, 0xEE);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x20);
  util_buffer_add_2b(p_buf, 0x07, 0x00);
  emit_REQUIRE_EQ(p_buf, 0xFC);
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_REQUIRE_EQ(p_buf, 0xDC);
  /* SHY abx, used by Citadel. */
  emit_LDX(p_buf, k_imm, 0x01);
  emit_LDY(p_buf, k_imm, 0x87);
  util_buffer_add_3b(p_buf, 0x9C, 0x00, 0x7E);
  emit_LDA(p_buf, k_abs, 0x7E01);
  emit_REQUIRE_EQ(p_buf, 0x07);
  /* ANC imm, used by Repton 2. */
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0xFF);
  util_buffer_add_2b(p_buf, 0x0B, 0x80);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x80);
  emit_JMP(p_buf, k_abs, 0xCCC0);

  /* Test some of the simpler BCD behavior. */
  set_new_index(p_buf, 0x0CC0);
  emit_SED(p_buf);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x15);
  emit_ADC(p_buf, k_imm, 0x14);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_EQ(p_buf, 0x30);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_ADC(p_buf, k_imm, 0x21);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x01);

  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_SBC(p_buf, k_imm, 0x14);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x16);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x21);
  emit_SBC(p_buf, k_imm, 0x80);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_EQ(p_buf, 0x40);

  emit_CLD(p_buf);
  emit_JMP(p_buf, k_abs, 0xCD10);

  /* Test ROR flags for memory-based RORs. */
  set_new_index(p_buf, 0x0D10);
  emit_CLC(p_buf);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_LDA(p_buf, k_imm, 0x81);   /* Sets NF, spotting failure to clear it. */
  emit_STA(p_buf, k_abs, 0x1001);
  emit_ROR(p_buf, k_abx, 0x1000);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 0);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_ROR(p_buf, k_abs, 0x1001);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCD50);

  /* Test for a JIT optimization bug that missed ROL A changing A. */
  set_new_index(p_buf, 0x0D50);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_SEC(p_buf);
  emit_ROL(p_buf, k_acc, 0);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCD80);

  /* Test for 16-bit wrap of aby mode. */
  set_new_index(p_buf, 0x0D80);
  emit_LDA(p_buf, k_imm, 0x65);
  emit_STA(p_buf, k_zpg, 0x5E);
  emit_LDY(p_buf, k_imm, 0xFB);
  emit_LDA(p_buf, k_aby, 0xFF63);
  emit_REQUIRE_EQ(p_buf, 0x65);
  emit_JMP(p_buf, k_abs, 0xCDC0);

  /* Test idx mode fetch where the 16-bit address wraps at 0xFF / 0x00.
   * Yes, something actually hit this: Camelot. See NOTES.games.
   */
  set_new_index(p_buf, 0x0DC0);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0xFF);
  emit_LDA(p_buf, k_imm, 0xFB);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_LDA(p_buf, k_idx, 0xFE);
  emit_REQUIRE_EQ(p_buf, 0x7D);
  emit_JMP(p_buf, k_abs, 0xCE00);

  /* Test hardware register access via the indirect modes. */
  set_new_index(p_buf, 0x0E00);
  emit_LDA(p_buf, k_imm, 0xFE);
  emit_STA(p_buf, k_zpg, 0x21);
  emit_LDA(p_buf, k_imm, 0x4A);
  emit_STA(p_buf, k_zpg, 0x20);
  emit_LDA(p_buf, k_imm, 0xA3);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_idx, 0x20);   /* idx mode write to $FE4A. */
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0x20);   /* idy mode read to $FE4A. */
  emit_REQUIRE_EQ(p_buf, 0xA3);
  emit_LDA(p_buf, k_imm, 0);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_idy, 0x20);   /* idy mode read to $FE4A. */
  emit_REQUIRE_EQ(p_buf, 0xA3);
  emit_JMP(p_buf, k_abs, 0xCE40);

  /* Test idy mode load with a known Y value. This is a JIT optimization
   * scenario.
   */
  set_new_index(p_buf, 0x0E40);
  emit_LDY(p_buf, k_imm, 0xFF);
  emit_STY(p_buf, k_zpg, 0x21);
  emit_LDY(p_buf, k_imm, 0xFD);
  emit_STY(p_buf, k_zpg, 0x20);
  emit_JMP(p_buf, k_abs, 0xCE50);
  set_new_index(p_buf, 0x0E50);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0x20);   /* idy mode read to $FFFD. */
  emit_LDY(p_buf, k_imm, 0xFF);
  emit_REQUIRE_EQ(p_buf, 0xC0);
  emit_JMP(p_buf, k_abs, 0xCE80);

  /* Test for a JIT bug that existed with merged opcodes up against block
   * boundaries.
   */
  set_new_index(p_buf, 0x0E80);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_LDX(p_buf, k_imm, 0xFE);
  emit_JMP(p_buf, k_abs, 0xCE8A); /* Start a new block after the 3 LSR. */
  emit_LSR(p_buf, k_acc, 0);
  emit_LSR(p_buf, k_acc, 0);
  emit_LSR(p_buf, k_acc, 0);
  emit_INX(p_buf);
  emit_BNE(p_buf, -6);
  emit_REQUIRE_EQ(p_buf, 0x1F);
  emit_JMP(p_buf, k_abs, 0xCEC0);

  /* Test for a JIT bug with self-modification of a NOP and following
   * instruction.
   */
  set_new_index(p_buf, 0x0EC0);
  emit_JSR(p_buf, 0x3090);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_abs, 0x3091);
  emit_STA(p_buf, k_abs, 0x3092);
  emit_LDX(p_buf, k_imm, 0xFE);
  emit_JSR(p_buf, 0x3090);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCF00);

  /* Test for a JIT bug with lost NZ flags update after merged ROL. */
  set_new_index(p_buf, 0x0F00);
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_ORA(p_buf, k_imm, 0x08);
  emit_CLC(p_buf);
  emit_ROL(p_buf, k_acc, 0);
  emit_ROL(p_buf, k_acc, 0);
  emit_ROL(p_buf, k_acc, 0);
  emit_ROL(p_buf, k_acc, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCF40);

  /* Test for a JIT bug with incorrect elimination over a branch boundary. */
  set_new_index(p_buf, 0x0F40);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_LSR(p_buf, k_acc, 0);
  emit_LDA(p_buf, k_imm, 0x71);
  emit_BCS(p_buf, 5);
  emit_LDA(p_buf, k_imm, 0x91);
  emit_JMP(p_buf, k_abs, 0x0000);
  emit_REQUIRE_EQ(p_buf, 0x71);
  emit_JMP(p_buf, k_abs, 0xCF80);

  /* Test for a JIT bug with loss of NZ flags after INY -> LDY optimization. */
  set_new_index(p_buf, 0x0F80);
  emit_LDY(p_buf, k_imm, 0x7F);
  emit_INY(p_buf);
  emit_JSR(p_buf, 0xF0A0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCFC0);

  /* Test JIT idy indirect load elimination. */
  set_new_index(p_buf, 0x0FC0);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_STY(p_buf, k_zpg, 0xC1);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0xC0);
  emit_LDA(p_buf, k_idy, 0xC0);
  emit_LDA(p_buf, k_idy, 0xC0);
  emit_TAX(p_buf);
  emit_LDA(p_buf, k_imm, 0xC1);
  emit_STA(p_buf, k_zpg, 0xC0);
  emit_LDA(p_buf, k_idy, 0xC0);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xC0);
  emit_JMP(p_buf, k_abs, 0xD000);

  /* Tests for a JIT bug with incorrect propagation over TXA. */
  set_new_index(p_buf, 0x1000);
  emit_LDX(p_buf, k_imm, 0x33);
  emit_JMP(p_buf, k_abs, 0xD020);
  set_new_index(p_buf, 0x1020);
  emit_LDA(p_buf, k_imm, 0x44);
  emit_TXA(p_buf);
  emit_STA(p_buf, k_zpg, 0x34);
  emit_LDA(p_buf, k_zpx, 0x01);
  emit_REQUIRE_EQ(p_buf, 0x33);
  emit_JMP(p_buf, k_abs, 0xD040);

  /* Test for correct overflow flag recovery after tricky situations. */
  set_new_index(p_buf, 0x1040);
  /* Setup for the idy load. */
  emit_LDA(p_buf, k_imm, 0x4A);
  emit_STA(p_buf, k_zpg, 0xC0);
  emit_LDA(p_buf, k_imm, 0xFE);
  emit_STA(p_buf, k_zpg, 0xC1);
  /* Part 1: Self modifying code. */
  emit_JSR(p_buf, 0x30B0);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x30B7);
  emit_JSR(p_buf, 0x30B0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  /* Part 2: Fault + fixup + self modifying code. */
  emit_JSR(p_buf, 0x3180);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x3189);
  emit_JSR(p_buf, 0x3180);
  emit_REQUIRE_OF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD080);

  /* Test for a JIT crash in the fault + fixup BCD handling. */
  set_new_index(p_buf, 0x1080);
  emit_SEI(p_buf);
  emit_SED(p_buf);
  emit_JMP(p_buf, k_abs, 0xD085);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_ADC(p_buf, k_imm, 0x89);
  emit_REQUIRE_EQ(p_buf, 0x91);
  emit_CLD(p_buf);
  emit_JMP(p_buf, k_abs, 0xD0C0);

  /* Test for a JIT bug in carry flag + branch handling. */
  set_new_index(p_buf, 0x10C0);
  emit_CLC(p_buf);
  emit_JMP(p_buf, k_abs, 0xD0C4);
  emit_LDA(p_buf, k_imm, 0x03);
  emit_ROR(p_buf, k_acc, 0);
  emit_INX(p_buf);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD100);

  /* Test for a JIT bug in the ordering of fixup application. */
  set_new_index(p_buf, 0x1100);
  emit_CLC(p_buf);
  emit_JMP(p_buf, k_abs, 0xD104);
  emit_LDA(p_buf, k_imm, 0xE0);
  emit_ADC(p_buf, k_imm, 0x7F);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_STX(p_buf, k_zpg, 0xC0);
  emit_PHA(p_buf);                /* Try and run the buffer out of space. */
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_PHA(p_buf);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_STX(p_buf, k_zpg, 0xC1);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_ADC(p_buf, k_imm, 0x00);
  emit_REQUIRE_EQ(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD140);

  /* Test for a JIT bug applying an incorrect fixup at an off-by-one address. */
  set_new_index(p_buf, 0x1140);
  emit_JSR(p_buf, 0x30E0); /* Sets OF to 1. */
  emit_ORA(p_buf, k_imm, 0xEE);   /* Clears host OF, not 6502 OF. */
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x30E6);
  emit_JSR(p_buf, 0x30E0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD180);

  /* Test for a JIT bug messing up a CMP + BCS combo after an ADC. */
  set_new_index(p_buf, 0x1180);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x12);
  emit_JMP(p_buf, k_abs, 0xD186);
  emit_ADC(p_buf, k_imm, 0x34);
  emit_CMP(p_buf, k_imm, 0x40);
  emit_REQUIRE_CF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD1C0);

  /* Test for a JIT bug optimizing a register zero that broke carry. */
  set_new_index(p_buf, 0x11C0);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_LDA(p_buf, k_imm, 0xDD);
  emit_JMP(p_buf, k_abs, 0xD1CA);
  emit_ADC(p_buf, k_imm, 0x55);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_ADC(p_buf, k_zpg, 0x40);
  emit_REQUIRE_EQ(p_buf, 0xF1);
  emit_JMP(p_buf, k_abs, 0xD200);

  /* Test for a JIT optimizer bug with SED + CLC + ADC. */
  set_new_index(p_buf, 0x1200);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JMP(p_buf, k_abs, 0xD205);
  emit_CLC(p_buf);
  emit_SED(p_buf);
  emit_ADC(p_buf, k_imm, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x11);
  emit_CLD(p_buf);
  emit_JMP(p_buf, k_abs, 0xD240);

  /* Test optimization of ROR zpg + flag eliminations. */
  set_new_index(p_buf, 0x1240);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_zpg, 0x40);
  emit_ROR(p_buf, k_zpg, 0x40);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xD280);

  /* Test JIT NZ flag recovery from memory. */
  set_new_index(p_buf, 0x1280);
  emit_JSR(p_buf, 0x30F0);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x30F7);
  emit_JSR(p_buf, 0x30F0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD2C0);

  /* Test JIT ROR NZ flag optimization corner case. */
  set_new_index(p_buf, 0x12C0);
  emit_JSR(p_buf, 0x31A0);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x31A9);
  emit_JSR(p_buf, 0x31A0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD300);

  /* Test SBC zpg plus optimizations. */
  set_new_index(p_buf, 0x1300);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_JMP(p_buf, k_abs, 0xD307);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x07);
  emit_SBC(p_buf, k_zpg, 0x40);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_EQ(p_buf, 0xF7);
  emit_JMP(p_buf, k_abs, 0xD340);

  /* Test for a JIT bug with incorrect carry flag optimization with SEC. */
  set_new_index(p_buf, 0x1340);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_JMP(p_buf, k_abs, 0xD347);
  emit_ASL(p_buf, k_zpg, 0x40);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_zpg, 0x40);
  emit_LDA(p_buf, k_zpg, 0x40);
  emit_REQUIRE_EQ(p_buf, 0x80);
  emit_JMP(p_buf, k_abs, 0xD380);

  /* Test for a JIT bug leaving CF in bad state if self-modify hits. */
  set_new_index(p_buf, 0x1380);
  emit_JSR(p_buf, 0x3110);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x3118);
  emit_JSR(p_buf, 0x3110);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xD3C0);

  /* Test for LDA abx dynamic operand, including bugs we hit. */
  set_new_index(p_buf, 0x13C0);
  emit_LDA(p_buf, k_imm, 0x4A);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x41);
  emit_JSR(p_buf, 0x3120);        /* Sets up an LDA abx as dynamic operand. */
  emit_INC(p_buf, k_abs, 0x312C); /* Self-modify later in the same block. */
  emit_JSR(p_buf, 0x3120);
  emit_LDA(p_buf, k_imm, 0xFE);   /* Arrange for fault in dynamic block. */
  emit_STA(p_buf, k_zpg, 0x41);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x42);
  emit_JSR(p_buf, 0x3120);
  emit_LDA(p_buf, k_zpg, 0x42);
  emit_REQUIRE_EQ(p_buf, 0x10);
  emit_JMP(p_buf, k_abs, 0xD400);

  /* Test for a JIT bug at the confluence of dynamic operand and known
   * constant propagation.
   */
  set_new_index(p_buf, 0x1400);
  emit_JSR(p_buf, 0x3140);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x9B);
  emit_JMP(p_buf, k_abs, 0xD440);

  /* Test for dynamic operand on a write instruction, and correct write
   * invalidation.
   */
  set_new_index(p_buf, 0x1440);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_zpg, 0x05);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x400A);
  emit_JSR(p_buf, 0x400A);
  emit_JSR(p_buf, 0x3150);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x400C);
  emit_JSR(p_buf, 0x400A);
  emit_LDA(p_buf, k_zpg, 0x05);
  emit_REQUIRE_EQ(p_buf, 0x82);
  emit_JMP(p_buf, k_abs, 0xD480);

  /* Test page crossing for the JMP (ind) addressing mode. */
  set_new_index(p_buf, 0x1480);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_abs, 0x10FF);
  emit_LDA(p_buf, k_imm, 0xD4);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0x1100);
  emit_JMP(p_buf, k_ind, 0x10FF);

  /* More involved BCD testing, including "illegal" values, "pointless" flags,
   * all tested by the Exile protected unpacker.
   */
  set_new_index(p_buf, 0x14C0);
  emit_SED(p_buf);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x88);
  emit_ADC(p_buf, k_imm, 0x78);
  /* ZF is based on the raw addition value. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x0F);
  emit_ADC(p_buf, k_imm, 0x0F);
  /* Huge invalid digits must still only carry once. */
  emit_REQUIRE_EQ(p_buf, 0x14);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_ADC(p_buf, k_imm, 0xF0);
  /* Huge result from invalid digits must still show carry. */
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x40);
  emit_CLD(p_buf);
  emit_JMP(p_buf, k_abs, 0xD500);

  /* Test stack wraps. */
  set_new_index(p_buf, 0x1500);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_TXS(p_buf);
  emit_JSR(p_buf, 0x30A0);      /* Just does RTS. */
  emit_TSX(p_buf);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_JMP(p_buf, k_abs, 0xD540);

  /* Test a few more undocumented opcodes uncovered by protected loaders. */
  set_new_index(p_buf, 0x1540);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0x00);
  /* LAX abs, used by the Zalaga loader. */
  util_buffer_add_3b(p_buf, 0xAF, 0xA0, 0xF0);
  emit_REQUIRE_EQ(p_buf, 0x60);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x60);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0x0A);
  emit_STA(p_buf, k_zpg, 0xF3);
  emit_LDY(p_buf, k_imm, 0x03);
  emit_LDA(p_buf, k_imm, 0x09);
  /* DCP idy, used by the Chip Buster loader. */
  util_buffer_add_2b(p_buf, 0xD3, 0xF0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_LDA(p_buf, k_zpg, 0xF3);
  emit_REQUIRE_EQ(p_buf, 0x09);
  /* SRE abs, used by the Zalaga loader. In fact, SRE $6C72 is the beginning of
   * the text string, Orlando M.Pilchard.
   */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_CLC(p_buf);
  util_buffer_add_3b(p_buf, 0x4F, 0xF3, 0x00);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_EQ(p_buf, 0x04);
  /* RLA idy, used by the Bone Cruncher loader. */
  util_buffer_add_2b(p_buf, 0x33, 0xF0);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_zpg, 0xF3);
  emit_REQUIRE_EQ(p_buf, 0x09);
  /* AHX idy, used by the Bone Cruncher loader. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_LDY(p_buf, k_imm, 0x03);
  util_buffer_add_2b(p_buf, 0x93, 0xF0);
  emit_LDA(p_buf, k_zpg, 0xF3);
  emit_REQUIRE_EQ(p_buf, 0x01);
  /* AXS, used by the Dune Rider tape loader. */
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x18);
  emit_LDX(p_buf, k_imm, 0xAA);
  util_buffer_add_2b(p_buf, 0xCB, 0x05);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x03);
  emit_REQUIRE_CF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xD5D0);

  /* Test reading VIA input registers. */
  set_new_index(p_buf, 0x15D0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE6B);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE63);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE6F);
  emit_LDA(p_buf, k_abs, 0xFE6F);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE6B);
  emit_LDA(p_buf, k_abs, 0xFE6F);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE6B);
  emit_JMP(p_buf, k_abs, 0xD600);

  /* Test an interesting JIT metadata bug where an incorrect invalidation
   * address led to JIT code corruption.
   */
  set_new_index(p_buf, 0x1600);
  emit_JSR(p_buf, 0x3160);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x3160);
  emit_JSR(p_buf, 0x3160);
  /* The issue here is that the host invalidation address for $3161 is left
   * stale from the first block that was compiled there, and not cleared when
   * the second (shorter) block was compiled.
   * This would hit a host(!) illegal instruction.
   */
  emit_STA(p_buf, k_abs, 0x3161);
  emit_JSR(p_buf, 0x3160);
  emit_JMP(p_buf, k_abs, 0xD620);

  /* Test VIA port A reads for real bus levels, and keyboard pull-low
   * dominance.
   */
  set_new_index(p_buf, 0x1620);
  emit_LDA(p_buf, k_imm, 0x0F);
  emit_STA(p_buf, k_abs, 0xFE42);
  emit_LDA(p_buf, k_imm, 3);      /* Keyboard auto-scan off. */
  emit_STA(p_buf, k_abs, 0xFE40);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE43); /* Port A to all outputs. */
  emit_STA(p_buf, k_abs, 0xFE4F); /* Scan for some non-existant key. */
  emit_LDA(p_buf, k_abs, 0xFE4F);
  emit_REQUIRE_EQ(p_buf, 0x7F);   /* Must still see a zero. */
  emit_JMP(p_buf, k_abs, 0xD640);

  /* Test idy mode with $FF. */
  set_new_index(p_buf, 0x1640);
  emit_LDA(p_buf, k_imm, 0xFE);
  emit_STA(p_buf, k_zpg, 0xFF);
  emit_LDA(p_buf, k_imm, 0xFB);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_LDA(p_buf, k_idy, 0xFF);   /* Read $FBFF. */
  emit_REQUIRE_EQ(p_buf, 0x7D);
  emit_JMP(p_buf, k_abs, 0xD680);

  /* Test idy mode invalidations. */
  set_new_index(p_buf, 0x1680);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0x3170);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x02);
  /* Flip INX to NOP. */
  emit_LDA(p_buf, k_imm, 0x70);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x31);
  emit_STA(p_buf, k_zpg, 0xF1);
  /* Cheesy way of setting Y=0 while avoiding "known-Y" optimization. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_PHA(p_buf);
  emit_PLA(p_buf);
  emit_TAY(p_buf);
  emit_LDA(p_buf, k_imm, 0xEA);
  emit_STA(p_buf, k_idy, 0xF0);
  /* Check it. */
  emit_JSR(p_buf, 0x3170);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x02);
  emit_JMP(p_buf, k_abs, 0xD6C0);

  /* Test for crossing a boundary to $C000. */
  set_new_index(p_buf, 0x16C0);
  /* Avoid optimizations. */
  emit_LDA(p_buf, k_imm, 0x08);
  emit_PHA(p_buf);
  emit_PLA(p_buf);
  emit_TAX(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abx, 0xBFF8);
  emit_LDA(p_buf, k_abx, 0xBFF8);
  /* Should have read out the PHP at $C000. */
  emit_REQUIRE_EQ(p_buf, 0x08);
  emit_STA(p_buf, k_abx, 0xBF01);
  emit_JMP(p_buf, k_abs, 0xD700);

  /* Test 16-bit wrap with indirect mode accesses. */
  set_new_index(p_buf, 0x1700);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x50);
  emit_STA(p_buf, k_zpg, 0x51);
  emit_LDA(p_buf, k_imm, 0xC3);
  emit_STA(p_buf, k_zpg, 0xFE);
  emit_LDY(p_buf, k_zpg, 0x50);
  emit_LDA(p_buf, k_idy, 0x50);
  emit_REQUIRE_EQ(p_buf, 0xC3);
  emit_LDA(p_buf, k_imm, 0xD7);
  emit_STA(p_buf, k_idy, 0x50);
  emit_LDA(p_buf, k_zpg, 0xFE);
  emit_REQUIRE_EQ(p_buf, 0xD7);
  emit_JMP(p_buf, k_abs, 0xD740);

  /* End of test. */
  set_new_index(p_buf, 0x1740);
  emit_EXIT(p_buf);

  /* Some program code that we copy to ROM at $F000 to RAM at $3000 */
  set_new_index(p_buf, 0x3000);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_INX(p_buf);
  emit_RTS(p_buf);

  /* This is close to one of the simplest self-modifying routines I found: the
   * Galaforce memory copy at first load.
   */
  set_new_index(p_buf, 0x3010);
  emit_LDY(p_buf, k_imm, 0x04);
  emit_LDA(p_buf, k_abx, 0x1A00); /* Jump target for both BNEs. */
  emit_STA(p_buf, k_abx, 0x0A00);
  emit_INX(p_buf);
  emit_BNE(p_buf, -9);
  emit_INC(p_buf, k_abs, 0x3014); /* Self-modifying. */
  emit_INC(p_buf, k_abs, 0x3017); /* Self-modifying. */
  emit_DEY(p_buf);
  emit_BNE(p_buf, -18);
  emit_RTS(p_buf);

  /* A block that self-modifies within itself. */
  set_new_index(p_buf, 0x3030);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0x3036);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_RTS(p_buf);

  /* Another block for us to modify. */
  set_new_index(p_buf, 0x3040);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_RTS(p_buf);

  /* Yet another block for us to modify. */
  set_new_index(p_buf, 0x3050);
  emit_INX(p_buf);
  emit_INY(p_buf);
  emit_RTS(p_buf);

  /* etc... */
  set_new_index(p_buf, 0x3060);
  emit_NOP(p_buf);
  emit_RTS(p_buf);

  /* etc... */
  set_new_index(p_buf, 0x3070);
  emit_INX(p_buf);
  emit_INY(p_buf);
  emit_RTS(p_buf);

  /* For branch dynamic operands. */
  set_new_index(p_buf, 0x3080);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_BEQ(p_buf, 1);
  emit_INX(p_buf);
  emit_RTS(p_buf);

  /* For a JIT test. */
  set_new_index(p_buf, 0x3090);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_RTS(p_buf);

  /* Statically placed RTS. */
  set_new_index(p_buf, 0x30A0);
  emit_RTS(p_buf);

  /* For a JIT overflag flag fixup test, part 1. */
  set_new_index(p_buf, 0x30B0);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_ADC(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_zpg, 0xE0);
  emit_NOP(p_buf);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_REQUIRE_OF(p_buf, 1);
  emit_RTS(p_buf);

  /* For another JIT overflow flag fixup test. */
  set_new_index(p_buf, 0x30E0);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x7E);
  emit_JMP(p_buf, k_abs, 0x30E6);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_RTS(p_buf);

  /* For JIT flag fixup recovery from ROL zpg. */
  set_new_index(p_buf, 0x30F0);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_zpg, 0xA0);
  emit_ROL(p_buf, k_zpg, 0xA0);
  emit_ROL(p_buf, k_zpg, 0xA0);
  emit_REQUIRE_NF(p_buf, 0);
  emit_RTS(p_buf);

  /* For JIT SBC optimization vs. self-modify. */
  set_new_index(p_buf, 0x3110);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x15);
  emit_JMP(p_buf, k_abs, 0x3116);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_RTS(p_buf);

  /* For JIT dynamic operand testing. */
  set_new_index(p_buf, 0x3120);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x10);   /* Modify 16 times to be sure of opt. */
  emit_LDA(p_buf, k_abx, 0x3000);
  emit_INC(p_buf, k_zpg, 0x42);
  emit_LDA(p_buf, k_idx, 0x40);   /* To fault-fixup at an interesting time. */
  emit_INC(p_buf, k_abs, 0x3125);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -13);
  emit_RTS(p_buf);

  /* For JIT dynamic operand testing. */
  set_new_index(p_buf, 0x3140);
  emit_LDY(p_buf, k_imm, 0x10);   /* Modify 16 times to be sure of opt. */
  emit_LDX(p_buf, k_imm, 0xAA);
  emit_STX(p_buf, k_zpg, 0x12);
  emit_DEC(p_buf, k_abs, 0x3143);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -10);
  emit_RTS(p_buf);

  /* For JIT write opcode dynamic operand testing. */
  set_new_index(p_buf, 0x3150);
  emit_LDY(p_buf, k_imm, 0x10);   /* Modify 16 times to be sure of opt. */
  emit_STY(p_buf, k_abs, 0x4000);
  emit_INC(p_buf, k_abs, 0x3153);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -9);
  emit_RTS(p_buf);

  /* For an interesting JIT metadata bug. */
  set_new_index(p_buf, 0x3160);
  emit_INX(p_buf);
  emit_STX(p_buf, k_zpg, 0xF0);
  emit_INY(p_buf);
  emit_STY(p_buf, k_zpg, 0xF1);
  emit_RTS(p_buf);

  /* For an invalidation test using the idy addressing mode. */
  set_new_index(p_buf, 0x3170);
  emit_INX(p_buf);
  emit_RTS(p_buf);

  /* For a JIT overflag flag fixup test, part 2. */
  set_new_index(p_buf, 0x3180);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_zpg, 0xE0);
  emit_LDA(p_buf, k_idy, 0xC0);
  emit_NOP(p_buf);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_REQUIRE_OF(p_buf, 1);
  emit_RTS(p_buf);

  /* For JIT ROR zpg flag optimization corner case. */
  set_new_index(p_buf, 0x31A0);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_zpg, 0xA0);
  emit_ROR(p_buf, k_zpg, 0xA0);
  emit_STA(p_buf, k_zpg, 0xA0);
  emit_ROR(p_buf, k_zpg, 0xA0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_RTS(p_buf);

  /* Need this byte here for a specific test. */
  set_new_index(p_buf, 0x3BFF);
  util_buffer_add_1b(p_buf, 0x7D);

  /* NMI routine. */
  set_new_index(p_buf, 0x3E00);
  emit_INC(p_buf, k_zpg, 0x00);
  emit_STA(p_buf, k_zpg, 0x04);
  emit_STX(p_buf, k_zpg, 0x05);
  emit_STY(p_buf, k_zpg, 0x06);
  emit_LDA(p_buf, k_imm, 0x42);
  emit_RTI(p_buf);

  /* IRQ routine. */
  set_new_index(p_buf, 0x3F00);
  emit_INC(p_buf, k_zpg, 0x00);
  emit_STA(p_buf, k_zpg, 0x04);
  emit_STX(p_buf, k_zpg, 0x05);
  emit_STY(p_buf, k_zpg, 0x06);
  emit_PLA(p_buf);
  emit_STA(p_buf, k_zpg, 0x01);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_abs, 0x4041);
  emit_BEQ(p_buf, 3);
  emit_JMP(p_buf, k_ind, 0x4040);
  emit_PHP(p_buf);
  emit_PLA(p_buf);
  emit_AND(p_buf, k_imm, 0x04);   /* Need I flag set. */
  emit_REQUIRE_ZF(p_buf, 0);
  emit_PLA(p_buf);
  emit_PHA(p_buf);
  emit_AND(p_buf, k_imm, 0x10);
  emit_BEQ(p_buf, 1);
  emit_RTI(p_buf);
  emit_PLA(p_buf);                /* For interrupts, RTI with I flag set. */
  emit_ORA(p_buf, k_imm, 0x04);
  emit_PHA(p_buf);
  emit_RTI(p_buf);

  fd = open("test.rom", O_CREAT | O_WRONLY, 0600);
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
  if (0 != close(fd)) {
    errx(1, "can't close output file descriptor for rom");
  }

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
