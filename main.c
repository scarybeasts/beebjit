#include "bbc.h"
#include "jit.h"
#include "x.h"

#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const size_t k_vector_reset = 0xfffc;

static void* jit_thread(void* p) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
 
  jit_enter(p_jit, k_vector_reset);

  exit(0);
}

int
main(int argc, const char* argv[]) {
  unsigned char* p_mem;
  unsigned char* p_mode7_mem;
  int fd;
  ssize_t read_ret;
  int ret;
  const char* os_rom_name = "os12.rom";
  const char* lang_rom_name = "basic.rom";
  unsigned char os_rom[k_bbc_rom_size];
  unsigned char lang_rom[k_bbc_rom_size];
  unsigned int debug_flags = 0;
  int i;
  pthread_t thread;
  struct x_struct* p_x;
  struct bbc_struct* p_bbc;
  struct jit_struct* p_jit;

  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (i + 1 < argc) {
      const char* val = argv[i + 1];
      if (strcmp(arg, "-o") == 0) {
        os_rom_name = val;
        ++i;
      } else if (strcmp(arg, "-l") == 0) {
        lang_rom_name = val;
        ++i;
      }
    }
    if (strcmp(arg, "-d") == 0) {
      debug_flags = 1;
    }
  }

  memset(os_rom, '\0', k_bbc_rom_size);
  memset(lang_rom, '\0', k_bbc_rom_size);

  fd = open(os_rom_name, O_RDONLY);
  if (fd < 0) {
    errx(1, "can't load OS rom");
  }
  read_ret = read(fd, os_rom, k_bbc_rom_size);
  if (read_ret != k_bbc_rom_size) {
    errx(1, "can't read OS rom");
  }
  close(fd);

  if (strlen(lang_rom_name) > 0) {
    fd = open(lang_rom_name, O_RDONLY);
    if (fd < 0) {
      errx(1, "can't load language rom");
    }
    read_ret = read(fd, lang_rom, k_bbc_rom_size);
    if (read_ret != k_bbc_rom_size) {
      errx(1, "can't read language rom");
    }
    close(fd);
  }

  p_bbc = bbc_create(os_rom, lang_rom);
  if (p_bbc == NULL) {
    errx(1, "bbc_create failed");
  }

  p_mem = bbc_get_mem(p_bbc);
  p_mode7_mem = bbc_get_mode7_mem(p_bbc);

  p_x = x_create(p_mode7_mem, k_bbc_mode7_width, k_bbc_mode7_height);
  if (p_x == NULL) {
    errx(1, "x_create failed");
  }

  p_jit = jit_create(p_mem);
  if (p_jit == NULL) {
    errx(1, "jit_create failed");
  }
  jit_jit(p_jit, 0xc000, k_bbc_rom_size, debug_flags);
  jit_jit(p_jit, 0x8000, k_bbc_rom_size, debug_flags);

  ret = pthread_create(&thread, NULL, jit_thread, p_jit);
  if (ret != 0) {
    errx(1, "couldn't create thread");
  }

  while (1) {
    x_render(p_x);
    sleep(1);
  }

  x_destroy(p_x);
  jit_destroy(p_jit);

  return 0;
}
