#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs_6502.h"
#include "emit_6502.h"
#include "util.h"

static const size_t k_rom_size = 16384;

static size_t
set_new_index(size_t index, size_t new_index) {
  assert(new_index >= index);
  return new_index;
}

static void
emit_REQUIRE_ZF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BEQ(p_buf, 1);
  } else {
    emit_BNE(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_NF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BMI(p_buf, 1);
  } else {
    emit_BPL(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_CF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BCS(p_buf, 1);
  } else {
    emit_BCC(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_OF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BVS(p_buf, 1);
  } else {
    emit_BVC(p_buf, 1);
  }
  emit_CRASH(p_buf);
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
  util_buffer_set_pos(p_buf, 0x0000);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0x01FD);
  emit_CMP(p_buf, k_imm, 0x36);   /* 1, BRK, I, Z */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0xFF);   /* Set all flags upon the PLP. */
  emit_STA(p_buf, k_abs, 0x01FD);
  emit_PLP(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0x01FD);
  emit_PLP(p_buf);
  emit_CMP(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_CLD(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE62); /* User VIA DDRB. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_abs, 0xFE6B); /* User VIA ACR to PB7 mode. */
  emit_LDA(p_buf, k_abs, 0xFE60); /* User VIA ORB. */
  emit_CMP(p_buf, k_imm, 0xFF);   /* PB7 initial state should be 1. */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC040);

  /* Check TSX / TXS stack setup. */
  util_buffer_set_pos(p_buf, 0x0040);
  index = set_new_index(index, 0x40);
  emit_TSX(p_buf);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_TXS(p_buf);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Check CMP vs. flags. */
  util_buffer_set_pos(p_buf, 0x0080);
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
  util_buffer_set_pos(p_buf, 0x00c0);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_imm, 0x03);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC100);

  /* Some SBC tests. */
  util_buffer_set_pos(p_buf, 0x0100);
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
  emit_SBC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC140);

  /* Some ROR / ROL tests. */
  util_buffer_set_pos(p_buf, 0x0140);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_acc, 0);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC180);

  /* Test BRK! */
  util_buffer_set_pos(p_buf, 0x0180);
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
  util_buffer_set_pos(p_buf, 0x01c0);
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
  util_buffer_set_pos(p_buf, 0x0200);
  emit_LDA(p_buf, k_imm, 0xFD);
  emit_STA(p_buf, k_zpg, 0x07);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x08);
  emit_LDX(p_buf, k_imm, 0x02);
  emit_LDA(p_buf, k_zpx, 0x05);
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
  util_buffer_set_pos(p_buf, 0x0240);
  emit_LDX(p_buf, k_imm, 0xd1);
  emit_LDA(p_buf, k_idx, 0x36);   /* Zero page wrap. */
  emit_CMP(p_buf, k_imm, 0xc0);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC280);

  /* Test simple JSR / RTS pair. */
  util_buffer_set_pos(p_buf, 0x0280);
  emit_JSR(p_buf,  0xC286);
  emit_JMP(p_buf, k_abs, 0xC2C0);
  emit_RTS(p_buf);

  /* Test BIT. */
  util_buffer_set_pos(p_buf, 0x02c0);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_INX(p_buf);
  emit_BIT(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC300);

  /* Test RTI. */
  util_buffer_set_pos(p_buf, 0x0300);
  emit_LDA(p_buf, k_imm, 0xC3);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x40);
  emit_PHA(p_buf);
  emit_PHP(p_buf);
  emit_RTI(p_buf);

  /* Test most simple self-modifying code. */
  util_buffer_set_pos(p_buf, 0x0340);
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
  util_buffer_set_pos(p_buf, 0x0380);
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
  util_buffer_set_pos(p_buf, 0x03C0);
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
  emit_JMP(p_buf, k_abs, 0xC400);

  /* Test some more involved self-modifying code situations. */
  util_buffer_set_pos(p_buf, 0x0400);
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
  util_buffer_set_pos(p_buf, 0x0440);
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
  util_buffer_set_pos(p_buf, 0x0480);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_JMP(p_buf, k_abs, 0xC488); /* L1 */
  emit_INX(p_buf);
  emit_BNE(p_buf, -3);            /* L1 here. */
  emit_DEY(p_buf);
  emit_BEQ(p_buf, -5);
  emit_JMP(p_buf, k_abs, 0xC4C0);

  /* Test self-modifying within a block. */
  util_buffer_set_pos(p_buf, 0x04C0);
  emit_JSR(p_buf, 0x3030);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC500);

  /* Test self-modifying code that may invalidate assumptions about instruction
   * flag optimizations.
   */
  util_buffer_set_pos(p_buf, 0x0500);
  emit_JSR(p_buf, 0x3040);
  emit_LDA(p_buf, k_imm, 0x60);
  /* Store RTS at $3042. */
  emit_STA(p_buf, k_abs, 0x3042);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0x3040);
  /* Test to see if the flags update went missing due to the self modifying
   * code changing flag expectations within the block.
   */
/*  p_mem[index++] = 0xd0; */ /* BNE (should be ZF=0) */
/*  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; */ /* FAIL */
  emit_JMP(p_buf, k_abs, 0xC540);

  /* Test various simple hardware register read / writes and initial state. */
  util_buffer_set_pos(p_buf, 0x0540);
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
  emit_JMP(p_buf, k_abs, 0xC580);

  /* Test writing to ROM memory. */
  util_buffer_set_pos(p_buf, 0x0580);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0x39);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_idy, 0x02); /* $C039 */
  emit_STA(p_buf, k_zpg, 0x04);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x02); /* $C039 */
  emit_LDA(p_buf, k_idy, 0x02);
  emit_CMP(p_buf, k_zpg, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_STA(p_buf, k_idx, 0x02); /* $C039 */
  emit_LDA(p_buf, k_imm, 0x1B);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_idy, 0x02); /* $801B */
  emit_STA(p_buf, k_zpg, 0x04);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x02); /* $801B */
  emit_LDA(p_buf, k_idy, 0x02);
  emit_CMP(p_buf, k_zpg, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC5C0);

  /* Test LDX with aby addressing, which was broken, oops! */
  util_buffer_set_pos(p_buf, 0x05C0);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x04);
  emit_LDX(p_buf, k_aby, 0xC5C0);
  emit_CPX(p_buf, k_imm, 0xBE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC600);

  /* Test a variety of additional high-address reads and writes of interest. */
  util_buffer_set_pos(p_buf, 0x0600);
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
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_STA(p_buf, k_idy, 0x02); /* $FFFF */
  emit_JMP(p_buf, k_abs, 0xC640);

  /* Test an interesting bug we had with self-modifying code where two
   * adjacent instructions are clobbered.
   */
  util_buffer_set_pos(p_buf, 0x0640);
  emit_NOP(p_buf);
  emit_JSR(p_buf, 0x3050);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x3050);
  emit_STA(p_buf, k_abs, 0x3051);
  emit_JSR(p_buf, 0x3050);
  emit_JMP(p_buf, k_abs, 0xC680);

  /* Test JIT invalidation through different write modes. */
  util_buffer_set_pos(p_buf, 0x0680);
  emit_JSR(p_buf, 0x3050);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xCA);   /* DEX */
  emit_STA(p_buf, k_abx, 0x3050);
  emit_JSR(p_buf, 0x3050);
  emit_CPX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0x88);   /* DEY */
  emit_STA(p_buf, k_aby, 0x3050);
  emit_JSR(p_buf, 0x3050);
  emit_CPY(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC6C0);

  /* Test JIT invalidation through remaining write modes. */
  util_buffer_set_pos(p_buf, 0x06C0);
  emit_LDA(p_buf, k_imm, 0xea);   /* NOP */
  emit_STA(p_buf, k_abs, 0x3050);
  emit_JSR(p_buf, 0x3050);
  emit_LDA(p_buf, k_imm, 0x50);
  emit_STA(p_buf, k_zpg, 0x8F);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_STA(p_buf, k_zpg, 0x90);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xe8);   /* INX */
  emit_STA(p_buf, k_idy, 0x8F);
  emit_JSR(p_buf, 0x3050);
  emit_CPX(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0xc8);   /* INY */
  emit_STA(p_buf, k_idx, 0x8F);
  emit_JSR(p_buf, 0x3050);
  emit_CPY(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC700);

  /* Test JIT recompilation bug leading to corrupted code generation.
   * Trigger condition is replacing an opcode resulting in short generated code
   * with one resulting in longer generated code.
   */
  util_buffer_set_pos(p_buf, 0x0700);
  emit_JSR(p_buf, 0x3070);
  emit_LDA(p_buf, k_imm, 0x48);   /* PHA */
  emit_STA(p_buf, k_abs, 0x3070);
  emit_LDA(p_buf, k_imm, 0x68);   /* PLA */
  emit_STA(p_buf, k_abs, 0x3071);
  emit_JSR(p_buf, 0x3070);
  emit_JMP(p_buf, k_abs, 0xC740);

  /* Test a few simple VIA behaviors. */
  util_buffer_set_pos(p_buf, 0x0740);
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
  util_buffer_set_pos(p_buf, 0x0780);
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
  emit_BVC(p_buf, 1);             /* Skip CRASH to test RTI PC miss bug. */
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
  util_buffer_set_pos(p_buf, 0x0800);
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
  emit_JMP(p_buf, k_abs, 0xC840);

  /* Test dynamic operands for a branch instruction, and a no-operands
   * instruction.
    */
  util_buffer_set_pos(p_buf, 0x0840);
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
  util_buffer_set_pos(p_buf, 0x0840);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_INX(p_buf);
  /* ZF is now 1. This should clear ZF. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC8C0);

  /* Test that timers don't return the same value twice in a row. */
  util_buffer_set_pos(p_buf, 0x08C0);
  emit_LDA(p_buf, k_abs, 0xFE64);
  emit_CMP(p_buf, k_abs, 0xFE64);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC900);

  /* Test that the carry flag optimizations don't break anything. */
  util_buffer_set_pos(p_buf, 0x0900);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x00);
  emit_CMP(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC940);

  /* Tests for a bug where the NZ flags were updated from the wrong register. */
  util_buffer_set_pos(p_buf, 0x0940);
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
  util_buffer_set_pos(p_buf, 0x0980);
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
  util_buffer_set_pos(p_buf, 0x09C0);
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

  util_buffer_set_pos(p_buf, 0x0A00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x7F01);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0x42);
  emit_STA(p_buf, k_abx, 0x7F01);
  emit_CMP(p_buf, k_abs, 0x7F01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCA40);

  /* Test for an interesting bug in the interpreter where NOP was reading the
   * previous instruction's address.
   * We can actually use the i8271 floppy controller for the simplest case of
   * a read having an observable side effect.
   */
  util_buffer_set_pos(p_buf, 0x0A40);
  emit_LDA(p_buf, k_imm, 0x2C);
  emit_STA(p_buf, k_abs, 0xFE80); /* Command: get drive status. */
  emit_STA(p_buf, k_abs, 0xFE81); /* Parameter: spurious, effective no-op. */
  emit_NOP(p_buf);                /* Should be no-op! */
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_CMP(p_buf, k_imm, 0x10);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCA80);

  /* Test that paging ROMs invalidates any previous artifcats, e.g. JIT
   * compilation.
   */
  util_buffer_set_pos(p_buf, 0x0A80);
  emit_LDA(p_buf, k_imm, 0x0F);
  emit_STA(p_buf, k_abs, 0xFE30); /* Page in BASIC. */
  emit_JSR(p_buf, 0x8000);        /* A != 1 so this just RTS's. */
  emit_LDA(p_buf, k_imm, 0x0E);
  emit_STA(p_buf, k_abs, 0xFE30); /* Page in DFS. */
  emit_LDA(p_buf, k_imm, 0xA0);   /* Set up the BRK vector to $CAA0. */
  emit_STA(p_buf, k_abs, 0x4040);
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_abs, 0x4041);
  emit_JSR(p_buf, 0x8000);        /* Should hit BRK. */
  emit_CRASH(p_buf);
  util_buffer_set_pos(p_buf, 0x0AA0);
  emit_LDA(p_buf, k_imm, 0x00);   /* Unset BRK vector. */
  emit_STA(p_buf, k_abs, 0x4040);
  emit_STA(p_buf, k_abs, 0x4041);
  emit_JMP(p_buf, k_abs, 0xCAC0);

  /* Tests triggering a simple NMI. */
  util_buffer_set_pos(p_buf, 0x0AC0);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_imm, 0x00); /* 0 is an invalid command for the 8271. */
  emit_STA(p_buf, k_abs, 0xFE80);
  emit_TAY(p_buf);
  emit_BEQ(p_buf, -3);
  emit_JMP(p_buf, k_abs, 0xCB00);

  /* Tests a bug where SEI clobbered the carry flag in JIT mode. */
  util_buffer_set_pos(p_buf, 0x0B00);
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
  util_buffer_set_pos(p_buf, 0x0B40);
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
  util_buffer_set_pos(p_buf, 0x0B80);
  emit_LDA(p_buf, k_imm, 0x07);
  emit_CLC(p_buf);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_CLC(p_buf);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_imm, 0x03);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xCBC0);

  /* Test VIA PB7 handling works correctly. */
  util_buffer_set_pos(p_buf, 0x0BC0);
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

  util_buffer_set_pos(p_buf, 0x0C40);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDY(p_buf, k_imm, 0x43);
  emit_EXIT(p_buf);

  /* Some program code that we copy to ROM at $F000 to RAM at $3000 */
  util_buffer_set_pos(p_buf, 0x3000);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_INX(p_buf);
  emit_RTS(p_buf);

  /* This is close to one of the simplest self-modifying routines I found: the
   * Galaforce memory copy at first load.
   */
  util_buffer_set_pos(p_buf, 0x3010);
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
  util_buffer_set_pos(p_buf, 0x3030);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0x3036);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_RTS(p_buf);

  /* Another block for us to modify. */
  util_buffer_set_pos(p_buf, 0x3040);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_RTS(p_buf);

  /* Yet another block for us to modify. */
  util_buffer_set_pos(p_buf, 0x3050);
  emit_INX(p_buf);
  emit_INY(p_buf);
  emit_RTS(p_buf);

  /* etc... */
  util_buffer_set_pos(p_buf, 0x3060);
  emit_NOP(p_buf);
  emit_RTS(p_buf);

  /* etc... */
  util_buffer_set_pos(p_buf, 0x3070);
  emit_INX(p_buf);
  emit_INY(p_buf);
  emit_RTS(p_buf);

  /* For branch dynamic operands. */
  util_buffer_set_pos(p_buf, 0x3080);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_BEQ(p_buf, 1);
  emit_INX(p_buf);
  emit_RTS(p_buf);

  /* Need this byte here for a specific test. */
  util_buffer_set_pos(p_buf, 0x3BFF);
  util_buffer_add_1b(p_buf, 0x7d);

  /* IRQ routine. */
  util_buffer_set_pos(p_buf, 0x3F00);
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

  /* NMI routine. */
  util_buffer_set_pos(p_buf, 0x3E00);
  emit_INC(p_buf, k_zpg, 0x00);
  emit_STA(p_buf, k_zpg, 0x04);
  emit_STX(p_buf, k_zpg, 0x05);
  emit_STY(p_buf, k_zpg, 0x06);
  emit_LDA(p_buf, k_imm, 0x42);
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
  close(fd);

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
