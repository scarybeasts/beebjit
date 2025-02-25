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

  uint8_t* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) argc;
  (void) argv;

  (void) memset(p_mem, '\xf2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* NMI vector. */
  p_mem[0x3FFA] = 0x00;
  p_mem[0x3FFB] = 0x0D;
  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3FFC] = 0x00;
  p_mem[0x3FFD] = 0xC0;
  /* IRQ vector. */
  p_mem[0x3FFE] = 0x00;
  p_mem[0x3FFF] = 0xFF;

  /* Check instruction timings for page crossings in abx mode. */
  set_new_index(p_buf, 0x0000);
  emit_CYCLES_RESET(p_buf);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 8);      /* Latency of reset + read is 8 cycles. */
  emit_LDX(p_buf, k_imm, 0x01);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abx, 0x1000); /* LDA abx, no page crossing, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abx, 0x10FF); /* LDA abx, page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 13);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abx, 0x10FF); /* LDA abx, no page crossing, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abx, 0x1000); /* STA abx, no page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 13);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_abx, 0x10FF); /* STA abx, page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 13);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Check instruction timings for page crossings in idy mode. */
  set_new_index(p_buf, 0x0080);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0x00B0);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_abs, 0x00B1);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_idy, 0xB0);   /* LDA idy, no page crossing, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 13);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_idy, 0xB0);   /* STA idy, no page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_idy, 0xB0);   /* LDA idy, page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_CYCLES_RESET(p_buf);
  emit_STA(p_buf, k_idy, 0xB0);   /* STA idy, page crossing, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_JMP(p_buf, k_abs, 0xC0E0);

  /* Check instruction timings for branching. */
  set_new_index(p_buf, 0x00E0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_BEQ(p_buf, 0x17);          /* Branch, taken, page crossing, 4 cycles. */

  set_new_index(p_buf, 0x0100);
  /* This is the landing point for the BEQ above. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_CYCLES_RESET(p_buf);
  emit_BNE(p_buf, -2);            /* Branch, not taken, 2 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 10);
  emit_CYCLES_RESET(p_buf);
  emit_BEQ(p_buf, 0);             /* Branch, taken, 3 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 11);
  emit_JMP(p_buf, k_abs, 0xC130);

  /* Check simple instruction timings that hit 1Mhz peripherals. */
  set_new_index(p_buf, 0x0130);
  emit_LDA(p_buf, k_abs, 0xFE00); /* 1MHz sync. */
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE4A); /* Read SR, even cycle start, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_LDA(p_buf, k_abs, 0xFE00); /* 1MHz sync. */
  emit_CYCLES_RESET(p_buf);       /* Cycles == 0 after this. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* Cycles == 3 after this opcode. */
  emit_LDA(p_buf, k_abs, 0xFE4A); /* Read SR, odd cycle start, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 16);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE20); /* Read Video ULA, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_LDA(p_buf, k_abs, 0xFE00); /* 1MHz sync. */
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE00); /* Read CRTC, even cycle start, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_JMP(p_buf, k_abs, 0xC190);

  /* Check T1 timer tick values. */
  /* T1, latch (e.g.) 4, ticks 4... 3... 2... 1... 0... -1... 4... */
  set_new_index(p_buf, 0x0190);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0x06);
  emit_STA(p_buf, k_abs, 0xFE44); /* T1CL: 6. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE45); /* T1CH: 0, timer starts. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* T1CL: should be 4. */
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_abs, 0xFE44); /* T1CL: should be -1. */
  emit_STA(p_buf, k_abs, 0x1001);
  emit_LDA(p_buf, k_abs, 0xFE44); /* T1CL: should be 2. */
  emit_STA(p_buf, k_abs, 0x1002);

  emit_LDA(p_buf, k_abs, 0x1000);
  emit_REQUIRE_EQ(p_buf, 0x04);
  emit_LDA(p_buf, k_abs, 0x1001);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_LDA(p_buf, k_abs, 0x1002);
  emit_REQUIRE_EQ(p_buf, 0x02);
  emit_JMP(p_buf, k_abs, 0xC1E0);

  /* Check T2 timer tick values. */
  /* T2 ticks (e.g.) 4... 3... 2... 1... 0... FFFF (-1)... FFFE */
  set_new_index(p_buf, 0x01E0);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0x06);
  emit_STA(p_buf, k_abs, 0xFE48); /* T2CL: 6. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE49); /* T2CH: 0, timer starts. */
  emit_LDA(p_buf, k_abs, 0xFE48); /* T2CL: should be 4. */
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_abs, 0xFE48); /* T2CL: should be -1 (0xFF) */
  emit_STA(p_buf, k_abs, 0x1001);
  emit_LDA(p_buf, k_abs, 0xFE48); /* T2CL: should be 0xFA */
  emit_STA(p_buf, k_abs, 0x1002);

  emit_LDA(p_buf, k_abs, 0x1000);
  emit_REQUIRE_EQ(p_buf, 0x04);
  emit_LDA(p_buf, k_abs, 0x1001);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_LDA(p_buf, k_abs, 0x1002);
  emit_REQUIRE_EQ(p_buf, 0xFA);
  emit_JMP(p_buf, k_abs, 0xC230);

  /* Check an interrupt fires immediately when T1 expires. */
  set_new_index(p_buf, 0x0230);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* 2 cycles. At timer value 1. */
  emit_INC(p_buf, k_zpg, 0x00);   /* 5 cycles. At timer value 0, -1. */
                                  /* Interrupt here (3rd cycle of INC). */
  emit_INX(p_buf);                /* Used to check if interrupt is late. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x42);
  emit_JMP(p_buf, k_abs, 0xC260);

  /* Check T1 reload in continuous mode. */
  /* Also checks that IFR flag is visible the same VIA cycle the IRQ fires. */
  set_new_index(p_buf, 0x0260);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Write ACR, TIMER1 continuous mode. */
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE44); /* T1CL: 4. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE45); /* T1CH: 0, timer starts, IFR cleared. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* T1CL read: clear IFR. */
  emit_LDX(p_buf, k_abs, 0xFE4D); /* IFR. Should be TIMER1. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* T1CL read: clear IFR. */
  emit_LDY(p_buf, k_abs, 0xFE4D); /* IFR. Should be TIMER1. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Write ACR, TIMER1 one-shot mode. */
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x40);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x40);
  emit_JMP(p_buf, k_abs, 0xC2A0);

  /* Check an interrupt is delayed when hitting too late in an instruction. */
  set_new_index(p_buf, 0x02A0);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* 2 cycles. At timer value 1. */
  emit_LDA(p_buf, k_abs, 0x0000); /* 4 cycles. At timer value 0, -1. */
                                  /* Interrupt here (3rd cycle of LDA). */
  emit_INX(p_buf);                /* Used to check if interrupt is early. */
                                  /* Interrupt noticed here. */
  emit_INX(p_buf);                /* Used to check if interrupt is late. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x43);
  emit_JMP(p_buf, k_abs, 0xC2D0);

  /* Test that a pending interrupt fires the instruction after a CLI. */
  set_new_index(p_buf, 0x02D0);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  /* Make sure we're out of interp.
   * Specifically, for testing, we need to be in the state where an IRQ is
   * pending, but we're executing in JIT with interrupts disabled at the time
   * the CLI is executed.
   */
  emit_LDY(p_buf, k_zpg, 0x00);
  emit_JMP(p_buf, k_abs, 0xC2DC);
  emit_NOP(p_buf);                /* In JIT; timer expired, IRQ pending. */
  emit_NOP(p_buf);
  emit_CLI(p_buf);                /* Clear I flag, but after IRQ check. */
  emit_DEX(p_buf);                /* IRQ should be raised after this DEX. */
  emit_DEX(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x41);
  emit_JMP(p_buf, k_abs, 0xC300);

  /* Test that a pending interrupt fires between CLI / SEI. */
  set_new_index(p_buf, 0x0300);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  /* Make sure we're out of interp. */
  emit_LDY(p_buf, k_zpg, 0x00);
  emit_JMP(p_buf, k_abs, 0xC30A);
  emit_NOP(p_buf);                /* IRQ pending, interrupts disabled. */
  emit_NOP(p_buf);
  emit_CLI(p_buf);                /* Clear I flag, but after IRQ check. */
  emit_SEI(p_buf);                /* IRQ should be raised after this SEI. */
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_JMP(p_buf, k_abs, 0xC340);

  /* Test that a pending interrupt fires immediately after RTI. */
  set_new_index(p_buf, 0x0340);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0xAA);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0xC3);   /* Push 0xC380 as RTI jump address. */
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x80);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);   /* Push 0x00 as RTI flags restore. */
  emit_PHA(p_buf);
  emit_RTI(p_buf);
  /* Continuation of RTI test case. */
  set_new_index(p_buf, 0x0380);
  emit_DEX(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0xAA);
  emit_JMP(p_buf, k_abs, 0xC3C0);

  /* Test an interrupt on the bounary between two "normal" instructions. */
  set_new_index(p_buf, 0x03C0);
  emit_LDX(p_buf, k_imm, 0x07);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 2. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* Timer value: 1, 0 (x0.5). */
  emit_INX(p_buf);                /* Timer value: 0 (x0.5), -1 (x0.5). */
  emit_INX(p_buf);                /* IFR at the start of this instruction. */
  emit_INX(p_buf);                /* IRQ called instead of INX. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x09);
  emit_JMP(p_buf, k_abs, 0xC400);

  /* Test an interrupt after a hardware register load. */
  set_new_index(p_buf, 0x0400);
  emit_LDY(p_buf, k_imm, 0xFF);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 0. */
  emit_LDY(p_buf, k_abs, 0xFE4B); /* Timer value: -1 (int / IRQ), 0, -1. */
  emit_INY(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x13);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_JMP(p_buf, k_abs, 0xC440);

  /* Test a deferred interrupt after a hardware register load. */
  set_new_index(p_buf, 0x0440);
  emit_LDY(p_buf, k_imm, 0xFF);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 1. */
  emit_LDY(p_buf, k_abs, 0xFE4B); /* Timer value: 0,  -1 (int), 1. */
  emit_INY(p_buf);
  emit_INY(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x13);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_JMP(p_buf, k_abs, 0xC480);

  /* Test 3 cycle branches (taken, no page crossing). */
  set_new_index(p_buf, 0x0480);
  emit_LDX(p_buf, k_imm, 0x39);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0x00);   /* Timer value: 1. */
  emit_CLI(p_buf);                /* Timer value: 0. */
  emit_BEQ(p_buf, 0);             /* Timer value: -1 (int), 1 (x0.5). */
  emit_INX(p_buf);                /* INX should execute and then IRQ. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x3A);
  emit_JMP(p_buf, k_abs, 0xC4C0);

  /* Test IRQ poll point at beginning of CLI. */
  set_new_index(p_buf, 0x04C0);
  emit_LDX(p_buf, k_imm, 0x7A);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 2. */
  emit_INX(p_buf);                /* Timer value: 1. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* Timer value: 0, -1 (x0.5). */ /* T1_INT */
  emit_CLI(p_buf);                /* CLI, then IRQ. */
  emit_INX(p_buf);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x7B);
  emit_JMP(p_buf, k_abs, 0xC500);

  /* Test IRQ poll point in PLP. */
  set_new_index(p_buf, 0x0500);
  emit_LDA(p_buf, k_imm, 0x04);   /* I flag. */
  emit_PHA(p_buf);
  emit_LDX(p_buf, k_imm, 0xD3);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 0. */
  emit_PLP(p_buf);                /* Timer value: -1 (T1_INT), 0. */
  emit_INX(p_buf);                /* IRQ should fire first. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0xD3);
  emit_JMP(p_buf, k_abs, 0xC540);

  /* Check T1 value at expiry in continuous mode. */
  set_new_index(p_buf, 0x0540);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Write ACR, TIMER1 continuous mode. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE44); /* T1CL: 1. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE45); /* T1CH: 0, timer starts, IFR cleared. */
  emit_LDX(p_buf, k_abs, 0xFE44); /* T1CL read. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Write ACR, TIMER1 one-shot mode. */
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_JMP(p_buf, k_abs, 0xC580);

  /* Check T1 value is correct after expiry and a latch rewrite. */
  set_new_index(p_buf, 0x0580);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* T1 one shot. */
  emit_LDA(p_buf, k_imm, 0x06);
  emit_STA(p_buf, k_abs, 0xFE44); /* T1CL: 6. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE45); /* T1CH: 0, timer starts. */
  emit_LDA(p_buf, k_abs, 0x0000); /* 6, 5. */
  emit_LDA(p_buf, k_abs, 0x0000); /* 4, 3. */
  emit_LDA(p_buf, k_abs, 0x0000); /* 2, 1. */
  emit_LDA(p_buf, k_abs, 0x0000); /* 0, -1. */
  emit_LDA(p_buf, k_imm, 0xFF);   /* 6. */
  emit_STA(p_buf, k_abs, 0xFE46); /* 5, 4, 3. */ /* T1LL */
  emit_LDX(p_buf, k_abs, 0xFE44); /* 2, 1, 0. */ /* T1CL */
  emit_LDY(p_buf, k_abs, 0xFE44); /* -1, 0xFF, 0xFE. */ /* T1CL */
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xFE);
  emit_JMP(p_buf, k_abs, 0xC5C0);

  /* Test T1 IFR vs. IFR write clearing IFR. */
  set_new_index(p_buf, 0x05C0);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0x7F);   /* 2. */
  emit_STA(p_buf, k_abs, 0xFE4D); /* 1, 0, -1. */ /* IFR: clear all. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* 5, 4, 3. */
  emit_REQUIRE_EQ(p_buf, 0xC0);   /* T1 IFR fired wins. */
  emit_JMP(p_buf, k_abs, 0xC600);

  /* Test T1 IFR vs. T1CL read clearing IFR. */
  set_new_index(p_buf, 0x0600);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_NOP(p_buf);                /* 2. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* 1, 0, -1 (IRQ). */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* 5, 4, 3. */ /* IFR. */
  emit_REQUIRE_EQ(p_buf, 0xC0);   /* T1 IFR fired wins. */
  emit_JMP(p_buf, k_abs, 0xC640);

  /* Test T1 IFR vs. T1CH write clearing IFR. */
  set_new_index(p_buf, 0x0640);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0x00);   /* 2. */
  emit_STA(p_buf, k_abs, 0xFE45); /* 1, 0, -1 (IRQ). */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* 5, 4, 3. */ /* IFR. */
  emit_REQUIRE_EQ(p_buf, 0xC0);   /* T1 IFR fired wins. */
  emit_JMP(p_buf, k_abs, 0xC680);

  /* Test IRQ raising just before the poll point of a stretched instruction. */
  set_new_index(p_buf, 0x0680);
  emit_LDX(p_buf, k_imm, 0xD4);
  emit_LDA(p_buf, k_imm, 0x03);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 3. */
  emit_INC(p_buf, k_abs, 0xFE62); /* User VIA DDRB, arbitrary 1Mhz address. */
                                  /* 2, 1+, 0 (R), -1 (W) (T1) (I), 5 (W). */
  emit_INX(p_buf);                /* IRQ should fire first. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0xD4);
  emit_JMP(p_buf, k_abs, 0xC6C0);

  /* Test existing IRQ just before the poll point of a stretched instruction,
   * where that cycle lowers the IRQ at the end.
   */
  set_new_index(p_buf, 0x06C0);
  emit_LDX(p_buf, k_imm, 0x91);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* Timer value: 2. */
  emit_DEC(p_buf, k_abs, 0xFE45); /* T1CH. */
                                  /* 1, 0+, -1 (R) (T1), 5 (W) (I), 0 (W). */
  emit_INX(p_buf);                /* IRQ should fire first. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x91);
  emit_JMP(p_buf, k_abs, 0xC700);

  /* Test an ACR write 0x40 -> 0x00 colliding with a T1 expiry. */
  set_new_index(p_buf, 0x0700);
  emit_LDA(p_buf, k_imm, 0x04);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0x40);   /* 4. */
  emit_STA(p_buf, k_abs, 0xFE4B); /* 3, 2, 1. */
  emit_LDX(p_buf, k_abs, 0xFE4D); /* 0, -1, 7. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* 6, 5, 4. */
  emit_NOP(p_buf);                /* 3. */
  emit_LDA(p_buf, k_imm, 0x00);   /* 2. */
  emit_STA(p_buf, k_abs, 0xFE4B); /* 1, 0, -1. */
  emit_LDY(p_buf, k_abs, 0xFE4D); /* 7, 6, 5. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* 4, 3, 2. */
  emit_NOP(p_buf);                /* 1. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* 0, -1, 7. */
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xC0);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xC0);
  emit_JMP(p_buf, k_abs, 0xC740);

  /* Test T2 freezing (putting into pulse count mode) and restarting. */
  set_new_index(p_buf, 0x0740);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* ACR: T2 running. */
  emit_LDA(p_buf, k_imm, 0x0A);
  emit_STA(p_buf, k_abs, 0xFE48); /* T2CL: 10. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE49); /* T2CH: 0. */
  emit_LDA(p_buf, k_imm, 0x20);   /* 10. */
  emit_STA(p_buf, k_abs, 0xFE4B); /* 9, 8, 7. */ /* ACR: T2 freezes at 6. */
  emit_LDX(p_buf, k_abs, 0xFE48);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* ACR: T2 running. */
  emit_LDY(p_buf, k_abs, 0xFE48); /* 6, 5, 4. */
  emit_LDA(p_buf, k_imm, 0x20);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Freeze. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE49);
  emit_LDA(p_buf, k_abs, 0xFE48);
  emit_REQUIRE_EQ(p_buf, 0x0A);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* ACR: T2 running. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* IFR, wait until hit. */
  emit_BEQ(p_buf, -5);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x06);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x04);
  emit_JMP(p_buf, k_abs, 0xC7C0);

  /* Test an interrupt landing exactly on the boundary of CLI / SEI pair. */
  set_new_index(p_buf, 0x07C0);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_zpg, 0x00);   /* Timer value: 1, 0 (x0.5). */
  emit_CLI(p_buf);                /* Timer value: 0 (x0.5), -1 (x0.5). */
  emit_SEI(p_buf);                /* IRQ should be raised after this SEI. */
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_JMP(p_buf, k_abs, 0xC800);

  /* Test that a self-modifying write still occurs if it co-incides with an
   * interrupt. Also test the timing in this case. Relevant to JIT mode.
   */
  set_new_index(p_buf, 0x0800);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x1001);
  emit_JSR(p_buf, 0x1000);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0xCA);   /* DEX */ /* Timer value: 1. */
  emit_STA(p_buf, k_abs, 0x1000); /* Timer value: 0, -1, self-modifies. */
  emit_LDX(p_buf, k_imm, 0x05);   /* Timer value: 4. */
  emit_JSR(p_buf, 0x1000);        /* Timer value: 3, 2, 1, 0, -1, 4, 3. */
  emit_LDY(p_buf, k_abs, 0xFE44); /* Timer value: 2, 1, 0. */
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x04);
  emit_TYA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x00);
  emit_JMP(p_buf, k_abs, 0xC840);

  /* Test that a large block (in terms of binary size) that is likely split
   * does not mess up timings.
   */
  set_new_index(p_buf, 0x0840);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_JSR(p_buf, 0xF000);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE44);
  emit_REQUIRE_EQ(p_buf, 0xD6);
  emit_JMP(p_buf, k_abs, 0xC880);

  /* Test a corner case with indirect access hitting a hardware register. */
  set_new_index(p_buf, 0x0880);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_imm, 0xFE);
  emit_STA(p_buf, k_zpg, 0x41);
  emit_LDA(p_buf, k_imm, 0x4A);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_LDA(p_buf, k_imm, 0x0E);
  emit_JSR(p_buf, 0xF000);
  emit_LDY(p_buf, k_imm, 0x00);   /* Timer value: 14. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* Timer value: 13, 12 (0.5). */
  emit_LDA(p_buf, k_idy, 0x40);   /* Timer value: 12 (0.5), 11, 10 (0.5), 9s. */
  emit_LDA(p_buf, k_abs, 0xFE44); /* Timer value: 8, 7 (0.5), 6 (s) */
  emit_REQUIRE_EQ(p_buf, 0x06);
  emit_JMP(p_buf, k_abs, 0xC8C0);

  /* Copy some ROM to RAM so we can test self-modifying code easier. */
  set_new_index(p_buf, 0x08C0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF2);
  emit_LDA(p_buf, k_imm, 0xE0);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_STA(p_buf, k_zpg, 0xF3);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_STA(p_buf, k_idy, 0xF2);
  emit_INY(p_buf);
  emit_BNE(p_buf, -7);
  emit_JMP(p_buf, k_abs, 0xC900);

  /* Test timing of abx mode load opcode with dynamic operand. */
  set_new_index(p_buf, 0x0900);
  emit_LDY(p_buf, k_imm, 0x10);
  emit_JSR(p_buf, 0x3000);
  emit_INC(p_buf, k_abs, 0x3001);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -9);
  emit_LDX(p_buf, k_imm, 0x6F);     /* No abx page crossing. */
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x3000);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_LDX(p_buf, k_imm, 0x70);     /* abx page crossing. */
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x3000);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x19);
  emit_JMP(p_buf, k_abs, 0xC940);

  /* Test for bug where an IRQ raise hits an instruction boundary, then
   * another timer timeout occurs immediately at another instruction boundary.
   */
  set_new_index(p_buf, 0x0940);
  emit_LDA(p_buf, k_imm, 0x09);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);                /* T1: 9. */
  emit_LDA(p_buf, k_imm, 0x01);   /* T2 to 0x02. */ /* T1: 8. */
  emit_STA(p_buf, k_abs, 0xFE48); /* T1: 7, 6, 5. */
  emit_LDA(p_buf, k_imm, 0x00);   /* T1: 4. */
  emit_STA(p_buf, k_abs, 0xFE49); /* T1: 3, 2, 1. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* T1: 0, -1 (x0.5), IRQ. */ /* T2: 1, 0.5. */
  emit_NOP(p_buf);                /* T2: 0, -0.5. Timeout (no IRQ). */
  emit_SEI(p_buf);
  emit_JMP(p_buf, k_abs, 0xC980);

  /* Test that we don't accidentally cycle stretch $FF00 - $FFFF. */
  set_new_index(p_buf, 0x0980);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abs, 0xFF00); /* LDA abs, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_JMP(p_buf, k_abs, 0xC9C0);

  /* Test a T1 latch value change co-inciding with a timer expiry / reload. */
  set_new_index(p_buf, 0x09C0);
  emit_LDA(p_buf, k_imm, 0x06);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0x40);   /* Set T1 continuous. */ /* T1: 6 */
  emit_STA(p_buf, k_abs, 0xFE4B); /* T1: 5, 4, 3. */
  emit_LDA(p_buf, k_imm, 0x10);   /* T1: 2 */
  emit_STA(p_buf, k_abs, 0xFE44); /* T1: 1, 0, -1. */
  emit_LDA(p_buf, k_abs, 0xFE44);
  /* The reloaded latch must be based on the value we wrote the same cycle as
   * the expiry / reload.
   */
  emit_REQUIRE_EQ(p_buf, 0x0E);
  emit_JMP(p_buf, k_abs, 0xCA00);

  /* Test a corner case from the Exile disc protected loader: invalid opcode
   * gets compiled but then invalidated before it executes.
   */
  set_new_index(p_buf, 0x0A00);
  /* Make sure timer doesn't disturb us by bouncing into interp. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE45);
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x3010);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 26);
  emit_JMP(p_buf, k_abs, 0xCA40);

  /* Test for an interesting bug in the interpreter where NOP was reading the
   * previous instruction's address.
   */
  set_new_index(p_buf, 0x0A40);
  /* Get T2 running. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE68);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE69);
  /* Clear T2 IER / IFR. */
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE6D);
  emit_STA(p_buf, k_abs, 0xFE6E);
  /* Start T2 again, soon to expire. */
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_abs, 0xFE68);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE69);
  /* Write addr $FE68; read of $FE68 would clear IFR. */
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_abs, 0xFE68);
  emit_NOP(p_buf);                /* Should be no-ops! */
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  /* IFR should still be high. */
  emit_LDA(p_buf, k_abs, 0xFE6D);
  emit_AND(p_buf, k_imm, 0x20);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xCA80);

  /* Test timing of an undocumented opcode that bounces into interp. */
  set_new_index(p_buf, 0x0A80);
  emit_CYCLES_RESET(p_buf);
  /* XAA #imm, 2 cycles. */
  util_buffer_add_2b(p_buf, 0x8B, 0x00);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 10);
  emit_JMP(p_buf, k_abs, 0xCAC0);

  /* Test that countdown expiry doesn't interfere with flags. */
  set_new_index(p_buf, 0x0AC0);
  emit_LDA(p_buf, k_imm, 0xE0);
  emit_STA(p_buf, k_zpg, 0x50);
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_zpg, 0x51);
  emit_LDA(p_buf, k_imm, 0x08);
  emit_JSR(p_buf, 0xF000);
  emit_NOP(p_buf);                /* T1: 8 */
  emit_NOP(p_buf);                /* T1: 7 */
  emit_LDA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x00);
  /* Use an indirect jump to ensure this jumps to a different JIT block. */
  emit_JMP(p_buf, k_ind, 0x0050);
  set_new_index(p_buf, 0x0AE0);
  /* New JIT block. Countdown expiry will fire at the start of this block. */
  emit_CLI(p_buf);                /* T1: 1 */
  emit_NOP(p_buf);                /* T1: 0 */
  emit_NOP(p_buf);                /* T1: -1 */
  emit_NOP(p_buf);
  emit_LDA(p_buf, k_zpg, 0x14);
  /* Require that the IRQ handler entered with ZF.
   * The bug corrupted the NZ flags.
   */
  emit_REQUIRE_EQ(p_buf, 0x22);
  emit_JMP(p_buf, k_abs, 0xCB00);

  /* Test page crossing timings with the aby mode. */
  set_new_index(p_buf, 0x0B00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_aby, 0x10FF); /* LDA aby, no page crossing, 4 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 12);
  emit_JMP(p_buf, k_abs, 0xCB40);

  /* Check instruction timings with "known Y" optimization, part 1. */
  set_new_index(p_buf, 0x0B40);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_JMP(p_buf, k_abs, 0xCB45);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_CYCLES_RESET(p_buf);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_LDY(p_buf, k_imm, 0x02);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 18);
  emit_JMP(p_buf, k_abs, 0xCB80);

  /* Check instruction timings with "known Y" optimization, part 2. */
  set_new_index(p_buf, 0x0B80);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_JMP(p_buf, k_abs, 0xCB85);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_CYCLES_RESET(p_buf);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_LDY(p_buf, k_imm, 0x02);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 17);
  emit_JMP(p_buf, k_abs, 0xCBC0);

  /* Test timing of aby mode load opcode with dynamic operand. */
  set_new_index(p_buf, 0x0BC0);
  emit_LDY(p_buf, k_imm, 0x10);
  emit_JSR(p_buf, 0x3020);
  emit_INC(p_buf, k_abs, 0x3021);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -9);
  emit_LDY(p_buf, k_imm, 0x6F);     /* No aby page crossing. */
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x3020);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_LDY(p_buf, k_imm, 0x70);     /* aby page crossing. */
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x3020);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x19);
  emit_JMP(p_buf, k_abs, 0xCC00);

  /* Test timing of a mode abx RMW instruction.
   * Yes, this was somehow wrong in JIT, likely an uncaught regression for a
   * long time.
   */
  set_new_index(p_buf, 0x0C00);
  emit_CYCLES_RESET(p_buf);
  emit_ROR(p_buf, k_abx, 0x7001);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 15);
  emit_JMP(p_buf, k_abs, 0xCC40);

  /* Check instruction timings with "known Y" optimization, part 3. */
  set_new_index(p_buf, 0x0C40);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_CYCLES_RESET(p_buf);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_JMP(p_buf, k_abs, 0xCC52);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 18);
  emit_JMP(p_buf, k_abs, 0xCC80);

  set_new_index(p_buf, 0x0C80);
  emit_LDX(p_buf, k_imm, 0);
  emit_CYCLES_RESET(p_buf);
  emit_DEC(p_buf, k_zpx, 0xF0);     /* Mode zpx, RMW, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_JMP(p_buf, k_abs, 0xCCC0);

  /* Check T2 interrupt isn't late. */
  set_new_index(p_buf, 0x0CC0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x10);
  emit_LDA(p_buf, k_imm, 0x20);
  emit_STA(p_buf, k_abs, 0xFE4D);
  emit_LDA(p_buf, k_imm, 0xA0);
  emit_STA(p_buf, k_abs, 0xFE4E);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE48);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE49);
  emit_CLI(p_buf);                /* 2 cycles. At timer value 1. */
  emit_INC(p_buf, k_zpg, 0x00);   /* 5 cycles. At timer value 0, -1. */
                                  /* Interrupt here (3rd cycle of INC). */
  emit_INX(p_buf);                /* Used to check if interrupt is late. */
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_REQUIRE_EQ(p_buf, 0x01);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x42);
  emit_JMP(p_buf, k_abs, 0xCD00);

  /* Check VSYNC IRQ isn't indicated if it comes in right at the end of an IFR
   * read.
   */
  set_new_index(p_buf, 0x0D00);
  /* 2MHz video. */
  emit_LDA(p_buf, k_imm, 0x10);
  emit_STA(p_buf, k_abs, 0xFE20);
  /* CRTC R9, R4, R5 to 0 (1 line frame), R8 to 0 (interlace off), R7 to 1
   * (vsync), R0 to 3.
   */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x03);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_LDA(p_buf, k_imm, 0x09);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_LDA(p_buf, k_imm, 0x05);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_LDA(p_buf, k_imm, 0x08);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_LDA(p_buf, k_imm, 0x07);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x03);
  emit_STA(p_buf, k_abs, 0xFE01);
  /* Wait to settle.
   * We're in 2Mhz mode but these waits should be good enough if we switched
   * to 1MHz.
   */
  emit_LDX(p_buf, k_imm, 0x00);
  emit_DEX(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_BNE(p_buf, -6);
  /* R0 to 0. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_STA(p_buf, k_abs, 0xFE01);
  /* Wait to settle. */
  emit_DEX(p_buf);
  emit_BNE(p_buf, -3);
  /* IRQ on positive vsync edge. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE4C);
  /* Clear IRQs. */
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4D);
  /* R0 to 31 (32 ticks line). */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x1F);
  emit_STA(p_buf, k_abs, 0xFE01);
  /* R4 to 3. */
  emit_LDA(p_buf, k_imm, 0x04);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x03);
  emit_STA(p_buf, k_abs, 0xFE01);
  /* R7 to 1. */
  emit_LDA(p_buf, k_imm, 0x07);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0xFE01);
  /* New frame. */
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  /* End of IFR read co-incides with vsync / CA1. */
  emit_LDA(p_buf, k_abs, 0xFE4D);
  /* That's too late for the VIA to have returned it. */
  emit_REQUIRE_EQ(p_buf, 0x00);
  /* R7 to never firing. */
  emit_LDA(p_buf, k_imm, 0x07);
  emit_STA(p_buf, k_abs, 0xFE00);
  emit_LDA(p_buf, k_imm, 0x3F);
  emit_STA(p_buf, k_abs, 0xFE01);
  emit_JMP(p_buf, k_abs, 0xCDC0);

  /* Check the timing of a collapsed loop. */
  set_new_index(p_buf, 0x0DC0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x10);
  emit_LDA(p_buf, k_imm, 0x50);
  emit_JSR(p_buf, 0xF000);
  emit_CLI(p_buf);
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_zpg, 0x10);
  emit_CMP(p_buf, k_imm, 0x01);
  emit_BNE(p_buf, -6);
  emit_SEI(p_buf);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0xDE);
  emit_JMP(p_buf, k_abs, 0xCE00);

  /* Test timing of a self-modification at a post-branch countdown. */
  set_new_index(p_buf, 0x0E00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF0);
  /* INX; BEQ -3; LDX #$50; LDX #$60; RTS */
  emit_LDA(p_buf, k_imm, 0xE8);
  emit_STA(p_buf, k_abs, 0x2E00);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_abs, 0x2E01);
  emit_LDA(p_buf, k_imm, 0xFD);
  emit_STA(p_buf, k_abs, 0x2E02);
  emit_LDA(p_buf, k_imm, 0xA2);
  emit_STA(p_buf, k_abs, 0x2E03);
  emit_LDA(p_buf, k_imm, 0x50);
  emit_STA(p_buf, k_abs, 0x2E04);
  emit_LDA(p_buf, k_imm, 0xA2);
  emit_STA(p_buf, k_abs, 0x2E05);
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x2E06);
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x2E07);
  /* Call in. */
  emit_LDX(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0x2E00);
  /* Modify and call in again. */
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x2E03);
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x2E00);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x18);
  emit_TXA(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x61);
  /* Also do the same timing test at the very block start. */
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x2E00);
  emit_CYCLES_RESET(p_buf);
  emit_JSR(p_buf, 0x2E00);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x14);
  emit_JMP(p_buf, k_abs, 0xCE80);

  /* Check the timing of a collapsed DEY loop. */
  set_new_index(p_buf, 0x0E80);
  emit_LDY(p_buf, k_imm, 0x02);
  emit_CYCLES_RESET(p_buf);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -3);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x11);
  /* Loop that can't fit into countdown. */
  emit_LDA(p_buf, k_imm, 0x10);
  emit_JSR(p_buf, 0xF000);
  emit_LDY(p_buf, k_imm, 0x10);
  emit_CYCLES_RESET(p_buf);
  emit_DEY(p_buf);
  emit_BNE(p_buf, -3);
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 0x57);
  emit_JMP(p_buf, k_abs, 0xCEC0);

  /* Test that hardware I/O is tick-then-access for a read where a relevant
   * event falls right at the post-instruction boundary at the end of a
   * block.
   */
  set_new_index(p_buf, 0x0EC0);
  emit_JMP(p_buf, k_abs, 0xCEC7);
  emit_LDA(p_buf, k_abs, 0xFE80);
  emit_RTS(p_buf);
  /* Create block with just RTS. */
  emit_JSR(p_buf, 0xCEC6);
  /* Create block with just LDA $FE80. */
  emit_JSR(p_buf, 0xCEC3);
  /* NMI routine is just RTI. */
  emit_LDA(p_buf, k_imm, 0x40);
  emit_STA(p_buf, k_abs, 0x0D00);
  /* Set NMI timer. */
  emit_LDA(p_buf, k_imm, 14);
  emit_STA(p_buf, k_abs, 0xFEE4);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_JSR(p_buf, 0xCEC3);
  emit_REQUIRE_EQ(p_buf, 0x0C);   /* 8271 NMI and need data. */
  emit_JMP(p_buf, k_abs, 0xCF00);

  /* Test 1Mhz peripheral timing for a JIT encoded callback. */
  set_new_index(p_buf, 0x0F00);
  emit_LDA(p_buf, k_abs, 0xFE00); /* 1MHz sync. */
  emit_CYCLES_RESET(p_buf);
  emit_LDA(p_buf, k_abs, 0xFE4D); /* Read IFR, even cycle start, 6 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 14);
  emit_LDA(p_buf, k_abs, 0xFE00); /* 1MHz sync. */
  emit_CYCLES_RESET(p_buf);       /* Cycles == 0 after this. */
  emit_LDA(p_buf, k_zpg, 0x00);   /* Cycles == 3 after this opcode. */
  emit_LDA(p_buf, k_abs, 0xFE4D); /* Read IFR, odd cycle start, 5 cycles. */
  emit_CYCLES(p_buf);
  emit_REQUIRE_EQ(p_buf, 16);
  emit_JMP(p_buf, k_abs, 0xCF40);

  /* Test an SBC + register access that blew the uops-per-opcode quota. */
  set_new_index(p_buf, 0x0F40);
  emit_LDA(p_buf, k_imm, 0x04);
  emit_JSR(p_buf, 0xF000);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_SEC(p_buf);
  emit_SBC(p_buf, k_abs, 0xFE45);
  emit_REQUIRE_EQ(p_buf, 0xFF);
  emit_JMP(p_buf, k_abs, 0xCF80);

  /* Test PLP that activates interrupts, when there's an IRQ line high. */
  set_new_index(p_buf, 0x0F80);
  emit_SEI(p_buf);
  emit_LDA(p_buf, k_imm, 0x03);   /* No I flag; C and Z set. */
  emit_PHA(p_buf);
  emit_LDX(p_buf, k_imm, 0xBB);
  emit_LDA(p_buf, k_imm, 0xAA);
  emit_STA(p_buf, k_zpg, 0x12);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  emit_NOP(p_buf);                /* Timer value: 0. */
  emit_NOP(p_buf);                /* Timer value: -1 (T1_INT), 0. */
  emit_JMP(p_buf, k_abs, 0xCF94); /* New JIT block. */
  emit_PLP(p_buf);
  emit_INX(p_buf);
  emit_INX(p_buf);                /* IRQ here, before this INX. */
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0xBC);
  emit_JMP(p_buf, k_abs, 0xCFC0);

  /* Test a write to VIA IER that activates an interrupt. */
  set_new_index(p_buf, 0x0FC0);
  emit_SEI(p_buf);
  emit_PHA(p_buf);
  emit_LDX(p_buf, k_imm, 0x33);
  emit_LDA(p_buf, k_imm, 0xAA);
  emit_STA(p_buf, k_zpg, 0x12);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0xF000);
  emit_NOP(p_buf);                /* Timer value: 0. */
  emit_NOP(p_buf);                /* Timer value: -1 (T1_INT), 0. */
  emit_JMP(p_buf, k_abs, 0xCFD2); /* New JIT block. */
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E);
  emit_CLI(p_buf);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_abs, 0xFE4E);
  emit_INX(p_buf);
  emit_INX(p_buf);
  emit_LDA(p_buf, k_zpg, 0x12);
  emit_REQUIRE_EQ(p_buf, 0x34);
  emit_JMP(p_buf, k_abs, 0xD000);

  /* Exit sequence. */
  set_new_index(p_buf, 0x1000);
  emit_EXIT(p_buf);

  /* Some program code that we copy to ROM at $E000 to RAM at $3000 */
  /* For testing dynamic operand of abx mode. */
  set_new_index(p_buf, 0x2000);
  emit_LDA(p_buf, k_abx, 0x0080);
  emit_RTS(p_buf);

  /* For testing compile of invalidate opcode. */
  set_new_index(p_buf, 0x2010);
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x3015);
  /* KIL */
  util_buffer_add_1b(p_buf, 0x72);

  /* For testing dynamic operand of aby mode. */
  set_new_index(p_buf, 0x2020);
  emit_LDA(p_buf, k_aby, 0x0080);
  emit_RTS(p_buf);

  /* Routine to arrange for an TIMER1 based IRQ at a specific time. */
  /* Input: A is timer value desired at first post-RTS opcode. */
  set_new_index(p_buf, 0x3000);
  emit_SEI(p_buf);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 3);      /* A += 3 to account for 6 cycle RTS. */
  emit_STA(p_buf, k_abs, 0xFE44); /* T1CL: A + 3. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0x10);   /* Clear IRQ count. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE4B); /* Write ACR, one-shot T1. */
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, TIMER1 interrupt on. */
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0xFE45); /* T1CH: 0, timer starts, IFR cleared. */
  emit_RTS(p_buf);

  /* IRQ routine. */
  set_new_index(p_buf, 0x3F00);
  emit_INC(p_buf, k_zpg, 0x10);
  emit_STA(p_buf, k_zpg, 0x11);
  emit_STX(p_buf, k_zpg, 0x12);
  emit_STY(p_buf, k_zpg, 0x13);
  emit_PLA(p_buf);
  emit_STA(p_buf, k_zpg, 0x14);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x7F);
  emit_STA(p_buf, k_abs, 0xFE4E); /* Write IER, interrupts off. */
  emit_RTI(p_buf);

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
  if (close(fd) != 0) {
    errx(1, "can't close output file descriptor for rom");
  }

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
