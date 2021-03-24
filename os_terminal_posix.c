#include "os_terminal.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "util.h"

intptr_t
os_terminal_get_stdin_handle(void) {
  return fileno(stdin);
}

intptr_t
os_terminal_get_stdout_handle(void) {
  return fileno(stdout);
}

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

int
os_terminal_has_readable_bytes(intptr_t handle) {
  int bytes_avail;

  int ret = ioctl(handle, FIONREAD, &bytes_avail);
  if (ret != 0) {
    return 0;
  }

  assert(bytes_avail >= 0);
  return (bytes_avail > 0);
}

int
os_terminal_handle_read_byte(intptr_t handle, uint8_t* p_byte) {
  ssize_t ret = read(handle, p_byte, 1);
  if (ret != 1) {
    util_bail("failed to read byte from handle");
  }

  return 1;
}

int
os_terminal_handle_write_byte(intptr_t handle, uint8_t byte) {
  ssize_t ret = write(handle, &byte, 1);
  if (ret != 1) {
    util_bail("failed to write byte to handle");
  }

  return 1;
}

static void (*s_p_interrupt_callback)(void);

static void
sigint_handler(int signum) {
  if (signum != SIGINT) {
    _exit(1);
  }

  s_p_interrupt_callback();
}

void
os_terminal_set_ctrl_c_callback(void (*p_interrupt_callback)(void)) {
  struct sigaction sa;
  int ret;

  s_p_interrupt_callback = p_interrupt_callback;

  (void) memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = sigint_handler;
  ret = sigaction(SIGINT, &sa, NULL);
  if (ret != 0) {
    util_bail("sigaction failed");
  }
}
