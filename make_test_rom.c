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
  char* p_mem = malloc(k_rom_size);
  memset(p_mem, '\0', k_rom_size);

  // Reset vector: jump to 0x8000, start of OS ROM.
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;

  // Fail!
  p_mem[0x0000] = 0x2f;

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
