#include "bbc.h"
#include "state.h"
#include "x.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t read_ret;
  const char* os_rom_name = "os12.rom";
  const char* lang_rom_name = "basic.rom";
  const char* load_name = NULL;
  unsigned char os_rom[k_bbc_rom_size];
  unsigned char lang_rom[k_bbc_rom_size];
  int debug_flag = 0;
  int run_flag = 0;
  int print_flag = 0;
  int i;
  struct x_struct* p_x;
  struct bbc_struct* p_bbc;

  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (i + 1 < argc) {
      const char* val = argv[i + 1];
      if (strcmp(arg, "-os") == 0) {
        os_rom_name = val;
        ++i;
      } else if (strcmp(arg, "-lang") == 0) {
        lang_rom_name = val;
        ++i;
      } else if (strcmp(arg, "-load") == 0) {
        load_name = val;
        ++i;
      }
    }
    if (strcmp(arg, "-d") == 0) {
      debug_flag = 1;
    } else if (strcmp(arg, "-r") == 0) {
      run_flag = 1;
    } else if (strcmp(arg, "-p") == 0) {
      print_flag = 1;
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

  p_bbc = bbc_create(os_rom, lang_rom, debug_flag, run_flag, print_flag);
  if (p_bbc == NULL) {
    errx(1, "bbc_create failed");
  }

  if (load_name != NULL) {
    state_load(p_bbc, load_name);
  }

  p_x = x_create(p_bbc, k_bbc_mode7_width, k_bbc_mode7_height);
  if (p_x == NULL) {
    errx(1, "x_create failed");
  }

  bbc_run_async(p_bbc);

  x_launch_event_loop_async(p_x);

  while (1) {
    int ret;
    struct timespec ts = { 0, 16 * 1000 * 1000 };
    x_render(p_x);
    /* 16ms, or about 60fps. */
    ret = nanosleep(&ts, NULL);
    if (ret != 0) {
      if (ret != -1 || errno != EINTR) {
        errx(1, "nanosleep failed");
      }
    }
  }

  x_destroy(p_x);
  bbc_destroy(p_bbc);

  return 0;
}
