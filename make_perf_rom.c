#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
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
  int arg;
  ssize_t write_ret;
  size_t index = 0;
  size_t bytes = 0;
  char* p_mem = malloc(k_rom_size);
  memset(p_mem, '\xf2', k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;

  /* Copy ROM to RAM. */
  index = set_new_index(index, 0);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xbd; /* LDA $C100,X */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc1;
  p_mem[index++] = 0x9d; /* STA $1000,X */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x10;
  p_mem[index++] = 0xca; /* DEX */
  p_mem[index++] = 0xd0; /* BNE -9 */
  p_mem[index++] = 0xf7;
  p_mem[index++] = 0x4c; /* JMP $1000 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x10;

  index = set_new_index(index, 0x100);
  p_mem[index++] = 0xa9; /* LDA #$20 */
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x85; /* STA $01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xaa; /* TAX */
  p_mem[index++] = 0xa8; /* TAY */
  p_mem[index++] = 0x85; /* STA $30 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x85; /* STA $31 */
  p_mem[index++] = 0x31;
  for (arg = 1; arg < argc; ++arg) {
    int i;
    if (sscanf(argv[arg], "%x", &i) == 1) {
      p_mem[index++] = (unsigned char) i;
      bytes++;
    }
  }
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xd0; /* BNE */
  p_mem[index++] = 0xfd - bytes;
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0xd0; /* BNE */
  p_mem[index++] = 0xfa - bytes;
  p_mem[index++] = 0xe6; /* INC $30 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xd0; /* BNE */
  p_mem[index++] = 0xf6 - bytes;
  p_mem[index++] = 0xe6; /* INC $31 */
  p_mem[index++] = 0x31;
  p_mem[index++] = 0xd0; /* BNE */
  p_mem[index++] = 0xf2 - bytes;
  p_mem[index++] = 0x02; /* Done! */

  fd = open("perf.rom", O_CREAT | O_WRONLY, 0600);
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
