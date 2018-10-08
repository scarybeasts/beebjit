#include "bbc.h"
#include "state.h"
#include "test.h"
#include "video.h"
#include "x.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t read_ret;
  unsigned char os_rom[k_bbc_rom_size];
  unsigned char lang_rom[k_bbc_rom_size];
  int i;
  struct x_struct* p_x;
  struct bbc_struct* p_bbc;
  int x_fd;
  int bbc_fd;
  struct pollfd poll_fds[2];

  const char* os_rom_name = "os12.rom";
  const char* lang_rom_name = "basic.rom";
  const char* load_name = NULL;
  const char* opt_flags = "";
  const char* log_flags = "";
  int debug_flag = 0;
  int run_flag = 0;
  int print_flag = 0;
  int slow_flag = 0;
  int test_flag = 0;
  int debug_stop_addr = 0;
  int pc = 0;

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
      } else if (strcmp(arg, "-opt") == 0) {
        opt_flags = val;
        ++i;
      } else if (strcmp(arg, "-log") == 0) {
        log_flags = val;
        ++i;
      } else if (strcmp(arg, "-stopat") == 0) {
        (void) sscanf(val, "%x", &debug_stop_addr);
        ++i;
      } else if (strcmp(arg, "-pc") == 0) {
        (void) sscanf(val, "%x", &pc);
        ++i;
      }
    }
    if (strcmp(arg, "-d") == 0) {
      debug_flag = 1;
    } else if (strcmp(arg, "-r") == 0) {
      run_flag = 1;
    } else if (strcmp(arg, "-p") == 0) {
      print_flag = 1;
    } else if (strcmp(arg, "-s") == 0) {
      slow_flag = 1;
    } else if (strcmp(arg, "-t") == 0) {
      test_flag = 1;
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

  p_bbc = bbc_create(os_rom,
                     lang_rom,
                     debug_flag,
                     run_flag,
                     print_flag,
                     slow_flag,
                     opt_flags,
                     log_flags,
                     debug_stop_addr);
  if (p_bbc == NULL) {
    errx(1, "bbc_create failed");
  }

  if (test_flag) {
    test_do_tests(p_bbc);
    return 0;
  }

  if (load_name != NULL) {
    state_load(p_bbc, load_name);
  }
  if (pc != 0) {
    /* TODO: Need a more precision API, bbc_set_pc. */
    bbc_set_registers(p_bbc, 0, 0, 0, 0, 0x04, pc);
  }

  p_x = x_create(p_bbc, k_bbc_mode7_width, k_bbc_mode7_height);
  if (p_x == NULL) {
    errx(1, "x_create failed");
  }

  bbc_run_async(p_bbc);

  x_fd = x_get_fd(p_x);
  bbc_fd = bbc_get_fd(p_bbc);

  poll_fds[0].fd = x_fd;
  poll_fds[0].events = POLLIN;
  poll_fds[1].fd = bbc_fd;
  poll_fds[1].events = POLLIN;

  while (1) {
    int ret;
    poll_fds[0].revents = 0;
    poll_fds[1].revents = 0;
    /* 20 ms */
    ret = poll(&poll_fds[0], 2, 20);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      errx(1, "poll failed");
    }

    if (poll_fds[0].revents & POLLIN) {
      assert(ret > 0);
      /* No x_event_check here -- see below. */
    }
    if (poll_fds[1].revents & POLLIN) {
      assert(ret > 0);
      assert(bbc_has_exited(p_bbc));
      break;
    }

    /* TODO: should render at 50Hz, but also renders on keypress! */
    x_render(p_x);
    /* We need to call x_event_check unconditionally, in case a key event comes
     * in during X rendering. In that case, the data could be read from the
     * socket and placed in the event queue during standard X queue processing.
     * This would lead to a delayed event because the poll() wouldn't see it
     * in the socket queue.
     */
    x_event_check(p_x);
  }

  x_destroy(p_x);
  bbc_destroy(p_bbc);

  return 0;
}
