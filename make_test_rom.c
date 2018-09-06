#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const size_t k_rom_size = 16384;

static size_t set_new_index(size_t index, size_t new_index) {
  assert(new_index >= index);
  return new_index;
}

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t write_ret;
  size_t index = 0;
  char* p_mem = malloc(k_rom_size);
  memset(p_mem, '\xf2', k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;
  /* IRQ vector: also jumped to by the BRK instruction. */
  p_mem[0x3ffe] = 0xc0;
  p_mem[0x3fff] = 0xc1;

  /* Check PHP, including initial 6502 boot-up flags status. */
  index = set_new_index(index, 0);
  p_mem[index++] = 0x08; /* PHP */
  p_mem[index++] = 0xad; /* LDA $0100 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xc9; /* CMP #$34 */ /* I, BRK, 1 */
  p_mem[index++] = 0x34;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$ff */ /* Set all flags upon the PLP. */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x8d; /* STA $0100 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x28; /* PLP */
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x08; /* PHP */
  p_mem[index++] = 0xad; /* LDA $0100 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x28; /* PLP */
  p_mem[index++] = 0xc9; /* CMP #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C040 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc0;

  /* Check TSX / TXS stack setup. */
  index = set_new_index(index, 0x40);
  p_mem[index++] = 0xba; /* TSX */
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa2; /* LDX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x9a; /* TXS */
  p_mem[index++] = 0x4c; /* JMP $C080 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc0;

  /* Check CMP vs. flags. */
  index = set_new_index(index, 0x80);
  p_mem[index++] = 0xa2; /* LDX #$ee */
  p_mem[index++] = 0xee;
  p_mem[index++] = 0xe0; /* CPX #$ee */
  p_mem[index++] = 0xee;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x10; /* BPL (should be NF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xe0; /* CPX #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xe0; /* CPX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x90; /* BCC (should be CF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C0C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc0;

  /* Some ADC tests. */
  index = set_new_index(index, 0xc0);
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x69; /* ADC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xc9; /* CMP #$03 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x69; /* ADC #$7f */
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x90; /* BCC (should be CF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x69; /* ADC #$7f */
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x50; /* BVC (should be OF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C100 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc1;

  /* Some SBC tests. */
  index = set_new_index(index, 0x100);
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0xa9; /* LDA #$02 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xe9; /* SBC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x10; /* BPL (should be NF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x50; /* BVC (should be OF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xe9; /* SBC #$80 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x90; /* BCC (should be CF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0xa9; /* LDA #$10 */
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xe9; /* SBC #$7f */
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x90; /* BCC (should be CF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x50; /* BVC (should be OF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C140 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc1;

  /* Some ROR / ROL tests. */
  index = set_new_index(index, 0x140);
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0x6a; /* ROR A */
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C180 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc1;

  /* Test BRK! */
  index = set_new_index(index, 0x180);
  p_mem[index++] = 0xa2; /* LDX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x9a; /* TXS */
  p_mem[index++] = 0x00; /* BRK ($FFFE -> $C1C0) */
  p_mem[index++] = 0xf2; /* FAIL */

  /* Test shift / rotate instuction coalescing. */
  index = set_new_index(index, 0x1c0);
  p_mem[index++] = 0xa9; /* LDA #$05 */
  p_mem[index++] = 0x05;
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0xc9; /* CMP #$14 */
  p_mem[index++] = 0x14;
  p_mem[index++] = 0xf0; /* BEQ */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0x6a; /* ROR A */
  p_mem[index++] = 0x6a; /* ROR A */
  p_mem[index++] = 0x6a; /* ROR A */
  p_mem[index++] = 0xb0; /* BCS */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xc9; /* CMP #$22 */
  p_mem[index++] = 0x22;
  p_mem[index++] = 0xf0; /* BEQ */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C200 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc2;

  /* Test indexed zero page addressing. */
  index = set_new_index(index, 0x200);
  p_mem[index++] = 0xa9; /* LDA #$FE */
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x85; /* STA $07 */
  p_mem[index++] = 0x07;
  p_mem[index++] = 0xa9; /* LDA #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x85; /* STA $08 */
  p_mem[index++] = 0x08;
  p_mem[index++] = 0xa2; /* LDX #$02 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xb5; /* LDA $05,X */
  p_mem[index++] = 0x05;
  p_mem[index++] = 0xc9; /* CMP #$FE */
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xf0; /* BEQ */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa2; /* LDX #$D1 */
  p_mem[index++] = 0xd1;
  p_mem[index++] = 0xb5; /* LDA $37,X */ /* Zero page wrap. */
  p_mem[index++] = 0x37;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C240 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc2;

  /* Test indirect indexed zero page addressing. */
  index = set_new_index(index, 0x240);
  p_mem[index++] = 0xa1; /* LDA ($36,X) */ /* Zero page wrap. */
  p_mem[index++] = 0x36;
  p_mem[index++] = 0xc9; /* CMP #$C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xf0; /* BEQ */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C280 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc2;

  /* Test simple JSR / RTS pair. */
  index = set_new_index(index, 0x280);
  p_mem[index++] = 0x20; /* JSR $C286 */
  p_mem[index++] = 0x86;
  p_mem[index++] = 0xc2;
  p_mem[index++] = 0x4c; /* JMP $C2C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc2;
  p_mem[index++] = 0x60; /* RTS */

  /* Test BIT. */
  index = set_new_index(index, 0x2c0);
  p_mem[index++] = 0xa9; /* LDA #$C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x24; /* BIT $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C300 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc3;

  /* Test RTI. */
  index = set_new_index(index, 0x300);
  p_mem[index++] = 0xa9; /* LDA #$c3 */
  p_mem[index++] = 0xc3;
  p_mem[index++] = 0x48; /* PHA */
  p_mem[index++] = 0xa9; /* LDA #$40 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0x48; /* PHA */
  p_mem[index++] = 0x48; /* PHP */
  p_mem[index++] = 0x40; /* RTI */

  /* Test most simple self-modifying code. */
  index = set_new_index(index, 0x340);
  p_mem[index++] = 0xa9; /* LDA #$60 */ /* RTS */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $2000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x20; /* JSR $2000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xa9; /* LDA #$E8 */ /* INX */
  p_mem[index++] = 0xe8;
  p_mem[index++] = 0x8d; /* STA $2000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xa9; /* LDA #$60 */ /* RTS */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $2001 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xa2; /* LDX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x20; /* JSR $2000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C380 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc3;

  /* Test self-modifying an operand of an opcode. */
  index = set_new_index(index, 0x380);
  /* Stores LDA #$00; RTS at $1000. */
  p_mem[index++] = 0xa9; /* LDA #$a9 */ /* LDA */
  p_mem[index++] = 0xa9;
  p_mem[index++] = 0x8d; /* STA $1000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xa9; /* LDA #$00 */ /* #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x8d; /* STA $1001 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xa9; /* LDA #$60 */ /* RTS */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $1002 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0x20; /* JSR $1000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  /* Modify LDA #$00 at $1000 to be LDA #$01. */
  p_mem[index++] = 0xa9; /* LDA #$01 */ /* #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $1001 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0x20; /* JSR $1000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C3C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc3;

  /* Copy some ROM to RAM so we can test self-modifying code easier.
   * This copy uses indirect Y addressing which wasn't actually previously
   * tested either.
   */
  index = set_new_index(index, 0x3c0);
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x85; /* STA #$f0 */
  p_mem[index++] = 0xf0;
  p_mem[index++] = 0x85; /* STA #$f2 */
  p_mem[index++] = 0xf2;
  p_mem[index++] = 0xa9; /* LDA #$f0 */
  p_mem[index++] = 0xf0;
  p_mem[index++] = 0x85; /* STA #$f1 */
  p_mem[index++] = 0xf1;
  p_mem[index++] = 0xa9; /* LDA #$30 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x85; /* STA #$f1 */
  p_mem[index++] = 0xf3;
  p_mem[index++] = 0xa0; /* LDY #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xb1; /* LDA ($f0),Y */
  p_mem[index++] = 0xf0;
  p_mem[index++] = 0x91; /* STA ($f2),Y */
  p_mem[index++] = 0xf2;
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0xd0; /* BNE -7 */
  p_mem[index++] = 0xf9;
  p_mem[index++] = 0x4c; /* JMP $C400 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc4;

  /* Test some more involved self-modifying code situations. */
  index = set_new_index(index, 0x400);
  p_mem[index++] = 0x20; /* JSR $3000 */ /* Sets X to 0, INX. */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3002 */ /* INX. */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$02 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  /* Flip INX to DEX at $3002. */
  p_mem[index++] = 0xa9; /* LDA #$CA */
  p_mem[index++] = 0xca;
  p_mem[index++] = 0x8d; /* STA $3002 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3000 */ /* Sets X to 0, DEX. */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  /* Flip LDX #$00 to LDX #$60 at $3000. */
  p_mem[index++] = 0xa9; /* LDA #$60 */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $3001 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3000 */ /* Sets X to 0x60, DEX. */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x10; /* BPL (should be NF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  /* The horrors: jump into the middle of an instruction. */
  p_mem[index++] = 0x20; /* JSR $3001 */ /* 0x60 == RTS */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C440 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc4;

  /* Tests a real self-modifying copy loop. */
  index = set_new_index(index, 0x440);
  p_mem[index++] = 0xa9; /* LDA #$E1 */
  p_mem[index++] = 0xe1;
  p_mem[index++] = 0x8d; /* STA $1CCC */
  p_mem[index++] = 0xcc;
  p_mem[index++] = 0x1c;
  p_mem[index++] = 0x20; /* JSR $3010 */
  p_mem[index++] = 0x10;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xad; /* LDA $0CCC */
  p_mem[index++] = 0xcc;
  p_mem[index++] = 0x0c;
  p_mem[index++] = 0xc9; /* CMP #$E1 */
  p_mem[index++] = 0xe1;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C480 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc4;

  /* Tests a sequence of forwards / backwards jumps that confused the JIT
   * address tracking.
   */
  index = set_new_index(index, 0x480);
  p_mem[index++] = 0xa2; /* LDX #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xa0; /* LDY #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x4c; /* JMP $C488 */ /* L1 */
  p_mem[index++] = 0x88;
  p_mem[index++] = 0xc4;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xd0; /* BNE -3 */ /* L1 here. */
  p_mem[index++] = 0xfd;
  p_mem[index++] = 0x88; /* DEY */
  p_mem[index++] = 0xf0; /* BEQ -5 */
  p_mem[index++] = 0xfb;
  p_mem[index++] = 0x4c; /* JMP $C4C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc4;

  /* Test self-modifying within a block. */
  index = set_new_index(index, 0x4c0);
  p_mem[index++] = 0x20; /* JSR $3030 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C500 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc5;

  /* Test self-modifying code that may invalidate assumptions about instruction
   * flag optimizations.
   */
  index = set_new_index(index, 0x500);
  p_mem[index++] = 0x20; /* JSR $3040 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$60 */
  p_mem[index++] = 0x60;
  /* Store RTS at $3042. */
  p_mem[index++] = 0x8d; /* STA $3042 */
  p_mem[index++] = 0x42;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x20; /* JSR $3040 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0x30;
  /* Test to see if the flags update went missing due to the self modifying
   * code changing flag expectations within the block.
   */
/*  p_mem[index++] = 0xd0; */ /* BNE (should be ZF=0) */
/*  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; */ /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C540 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc5;

  /* Test various simple hardware register read / writes. */
  index = set_new_index(index, 0x540);
  p_mem[index++] = 0xa9; /* LDA #$41 */
  p_mem[index++] = 0x41;
  p_mem[index++] = 0x8d; /* STA $FE4A */
  p_mem[index++] = 0x4a;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE4A */
  p_mem[index++] = 0x4a;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$41 */
  p_mem[index++] = 0x41;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa0; /* LDY #$CA */
  p_mem[index++] = 0xca;
  p_mem[index++] = 0xb9; /* LDA $FD80,Y */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xfd;
  p_mem[index++] = 0xc9; /* CMP #$41 */
  p_mem[index++] = 0x41;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa2; /* LDX #$0A */
  p_mem[index++] = 0x0a;
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0x7e; /* ROR $FE40,X */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xad; /* LDA $FE4A */
  p_mem[index++] = 0x4a;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$A0 */
  p_mem[index++] = 0xa0;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xce; /* DEC $FE4A */
  p_mem[index++] = 0x4a;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE4A */
  p_mem[index++] = 0x4a;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$9F */
  p_mem[index++] = 0x9f;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C580 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc5;

  /* Test writing to ROM memory. */
  index = set_new_index(index, 0x580);
  p_mem[index++] = 0xa9; /* LDA #$39 */
  p_mem[index++] = 0x39;
  p_mem[index++] = 0x85; /* STA $02 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xa9; /* LDA #$C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0x85; /* STA $03 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xb1; /* LDA ($02),Y */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0x85; /* STA $04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0x69; /* ADC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x91; /* STA ($02),Y */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xb1; /* LDA ($02),Y */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xc5; /* CMP $04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C5C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc5;

  /* Test LDX with aby addressing, which was broken, oops! */
  index = set_new_index(index, 0x5c0);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa0; /* LDY #$04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0xbe; /* LDX $C5C0,Y */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc5;
  p_mem[index++] = 0xe0; /* CPX #$BE */
  p_mem[index++] = 0xbe;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C600 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc6;

  /* Test a variety of additional high-address reads and writes of interest. */
  index = set_new_index(index, 0x600);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xbd; /* LDA $FBFF,X */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xfb;
  p_mem[index++] = 0xc9; /* CMP #$7D */
  p_mem[index++] = 0x7d;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x8d; /* STA $801B */
  p_mem[index++] = 0x1b;
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xad; /* LDA $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xce; /* DEC $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0x6e; /* ROR $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C640 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc6;

  /* Test an interesting bug we had with self-modifying code where two
   * adjacent instructions are clobbered.
   */
  index = set_new_index(index, 0x640);
  p_mem[index++] = 0xea; /* NOP */
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$60 */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x8d; /* STA $3051 */
  p_mem[index++] = 0x51;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C680 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc6;

  /* Test JIT invalidation through different write modes. */
  index = set_new_index(index, 0x680);
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa0; /* LDY #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$ca */ /* DEX opcode */
  p_mem[index++] = 0xca;
  p_mem[index++] = 0x9d; /* STA $3050,X */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$88 */ /* DEY opcode */
  p_mem[index++] = 0x88;
  p_mem[index++] = 0x99; /* STA $3050,Y */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xc0; /* CPY #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C6C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc6;

  /* Test JIT invalidation through remaining write modes. */
  index = set_new_index(index, 0x6c0);
  p_mem[index++] = 0xa9; /* LDA #$ea */ /* NOP opcode */
  p_mem[index++] = 0xea;
  p_mem[index++] = 0x8d; /* STA $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$50 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x85; /* STA $8F */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0xa9; /* LDA #$30 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x85; /* STA $90 */
  p_mem[index++] = 0x90;
  p_mem[index++] = 0xa0; /* LDY #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$e8 */ /* INX opcode */
  p_mem[index++] = 0xe8;
  p_mem[index++] = 0x91; /* STA ($8F),Y */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$c8 */ /* INY opcode */
  p_mem[index++] = 0xc8;
  p_mem[index++] = 0x81; /* STA ($8F,X) */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xc0; /* CPY #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C700 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc7;

  /* Test JIT recompilation bug leading to corrupted code generation.
   * Trigger condition is replacing an opcode resulting in short generated code
   * with one resulting in longer generated code.
   */
  index = set_new_index(index, 0x700);
  p_mem[index++] = 0x20; /* JSR $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$48 */ /* PHA */
  p_mem[index++] = 0x48;
  p_mem[index++] = 0x8d; /* STA $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$68 */ /* PLA */
  p_mem[index++] = 0x68;
  p_mem[index++] = 0x8d; /* STA $3071 */
  p_mem[index++] = 0x71;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C740 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc7;

  /* Test a few simple VIA behaviors. */
  index = set_new_index(index, 0x740);
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $FE62 */ /* User VIA DDRB */
  p_mem[index++] = 0x62;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FE */
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C780 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc7;

  index = set_new_index(index, 0x780);
  p_mem[index++] = 0x02; /* Done */

  /* Some program code that we copy to ROM at $f000 to RAM at $3000 */
  index = set_new_index(index, 0x3000);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x60; /* RTS */

  /* This is close to one of the simplest self-modifying routines I found: the
   * Galaforce memory copy at first load.
   */
  index = set_new_index(index, 0x3010);
  p_mem[index++] = 0xa0; /* LDY #$04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0xbd; /* LDA $1A00,X */ /* Jump target for both BNEs. */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x1a;
  p_mem[index++] = 0x9d; /* STA $0A00,X */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x0a;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xd0; /* BNE -9 */
  p_mem[index++] = 0xf7;
  p_mem[index++] = 0xee; /* INC $3014 */ /* Self-modifying. */
  p_mem[index++] = 0x14;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xee; /* INC $3017 */ /* Self-modifying. */
  p_mem[index++] = 0x17;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x88; /* DEY */
  p_mem[index++] = 0xd0; /* BNE -18 */
  p_mem[index++] = 0xee;
  p_mem[index++] = 0x60; /* RTS */

  /* A block that self-modifies within itself. */
  index = set_new_index(index, 0x3030);
  p_mem[index++] = 0xa9; /* LDA #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x8d; /* STA $3036 */
  p_mem[index++] = 0x36;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x60; /* RTS */

  /* Another block for us to modify. */
  index = set_new_index(index, 0x3040);
  p_mem[index++] = 0xa9; /* LDA #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x60; /* RTS */

  /* Yet another block for us to modify. */
  index = set_new_index(index, 0x3050);
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0x60; /* RTS */

  /* etc... */
  index = set_new_index(index, 0x3060);
  p_mem[index++] = 0xea; /* NOP */
  p_mem[index++] = 0x60; /* RTS */

  /* etc... */
  index = set_new_index(index, 0x3070);
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0x60; /* RTS */

  /* Need this byte here for a specific test. */
  index = set_new_index(index, 0x3bff);
  p_mem[index++] = 0x7d;

  fd = open("test.rom", O_CREAT | O_WRONLY, 0600);
  if (fd < 0) {
    errx(1, "can't open output rom");
  }
  write_ret = write(fd, p_mem, k_rom_size);
  if (write_ret != k_rom_size) {
    errx(1, "can't write output rom");
  }
  close(fd);

  return 0;
}
