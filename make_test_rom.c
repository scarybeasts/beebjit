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

  // Reset vector: jump to 0xC000, start of OS ROM.
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;
  // IRQ vector: also jumped to by the BRK instruction.
  p_mem[0x3ffe] = 0xc0;
  p_mem[0x3fff] = 0xc1;

  // Check TSX / TXS stack setup.
  index = set_new_index(index, 0);
  p_mem[index++] = 0xba; // TSX
  p_mem[index++] = 0xf0; // BEQ (should be ZF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xa2; // LDX #$ff
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x9a; // TXS
  p_mem[index++] = 0x4c; // JMP $C040
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc0;

  // Check PHP.
  index = set_new_index(index, 0x40);
  p_mem[index++] = 0xa9; // LDA #$00
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x08; // PHP
  p_mem[index++] = 0xad; // LDA $01ff
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xc9; // CMP #$32
  p_mem[index++] = 0x32;
  p_mem[index++] = 0xf0; // BEQ (should be ZF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x28; // PLP
  p_mem[index++] = 0x4c; // JMP $C080
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc0;

  // Check CMP vs. flags.
  index = set_new_index(index, 0x80);
  p_mem[index++] = 0xa2; // LDX #$ee
  p_mem[index++] = 0xee;
  p_mem[index++] = 0xe0; // CPX #$ee
  p_mem[index++] = 0xee;
  p_mem[index++] = 0xf0; // BEQ (should be ZF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xb0; // BCS (should be CF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x10; // BPL (should be NF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xe0; // CPX #$01
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xb0; // BCS (should be CF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xe0; // CPX #$ff
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x90; // BCC (should be CF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x4c; // JMP $C0C0
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc0;

  // Some ADC tests.
  index = set_new_index(index, 0xc0);
  p_mem[index++] = 0x38; // SEC
  p_mem[index++] = 0xa9; // LDA #$01
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x69; // ADC #$01
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xc9; // CMP #$03
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xf0; // BEQ (should be ZF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x69; // ADC #$7f
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x90; // BCC (should be CF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x70; // BVS (should be OF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x69; // ADC #$7f
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0xb0; // BCS (should be CF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x50; // BVC (should be OF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x4c; // JMP $C100
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc1;

  // Some SBC tests.
  index = set_new_index(index, 0x100);
  p_mem[index++] = 0x18; // CLC
  p_mem[index++] = 0xa9; // LDA #$02
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xe9; // SBC #$01
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; // BEQ (should be ZF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x10; // BPL (should be NF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xb0; // BCS (should be CF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x50; // BVC (should be OF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xe9; // SBC #$80
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x90; // BCC (should be CF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x70; // BVS (should be OF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x38; // SEC
  p_mem[index++] = 0xa9; // LDA #$10
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xe9; // SBC #$7f
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x90; // BCC (should be CF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x50; // BVC (should be OF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x4c; // JMP $C140
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc1;

  // Some ROR / ROL tests.
  index = set_new_index(index, 0x140);
  p_mem[index++] = 0xa9; // LDA #$01
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x38; // SEC
  p_mem[index++] = 0x6a; // ROR A
  p_mem[index++] = 0xd0; // BNE (should be ZF=0)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x30; // BMI (should be NF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0xb0; // BCS (should be CF=1)
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; // FAIL
  p_mem[index++] = 0x4c; // JMP $C180
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc1;

  // Test BRK!
  index = set_new_index(index, 0x180);
  p_mem[index++] = 0xa2; // LDX #$ff
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x9a; // TXS
  p_mem[index++] = 0x00; // BRK ($FFFE -> $C1C0)
  p_mem[index++] = 0xf2; // FAIL

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
  p_mem[index++] = 0xa2; /* LDX #$d1 */
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

  index = set_new_index(index, 0x280);
  p_mem[index++] = 0x02; /* Done */

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
