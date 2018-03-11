#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const size_t k_rom_size = 16384;

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t write_ret;
  size_t index;
  char* p_mem = malloc(k_rom_size);
  memset(p_mem, '\xf2', k_rom_size);

  // Reset vector: jump to 0xC000, start of OS ROM.
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;

  // Check TSX / TXS stack setup.
  index = 0;
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
  index = 0x40;
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
  p_mem[index++] = 0x4c; // JMP $C080
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc0;

  index = 0x80;
  p_mem[index++] = 0x02;

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
