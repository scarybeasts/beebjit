#include "os_terminal.h"

#include <termios.h>

#include <sys/ioctl.h>

void
os_terminal_setup(intptr_t handle) {
  int ret;
  struct termios termios;

  if (!isatty(handle)) {
    return;
  }

  ret = tcgetattr(handle, &termios);
  if (ret != 0) {
    util_bail("tcgetattr failed");
  }

  termios.c_lflag &= ~ICANON;
  termios.c_lflag &= ~ECHO;

  ret = tcsetattr(handle, TCSANOW, &termios);
  if (ret != 0) {
    util_bail("tcsetattr failed");
  }
}

uint64_t
os_terminal_readable_bytes(intptr_t handle) {
  int bytes_avail;

  int ret = ioctl(handle, FIONREAD, &bytes_avail);
  if (ret != 0) {
    return 0;
  }

  assert(bytes_avail >= 0);
  return bytes_avail;
}
