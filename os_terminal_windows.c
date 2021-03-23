#include "os_terminal.h"

#include "util.h"

#include <windows.h>

intptr_t
os_terminal_get_stdin_handle(void) {
  HANDLE ret = GetStdHandle(STD_INPUT_HANDLE);
  if (ret == INVALID_HANDLE_VALUE) {
    util_bail("can't get STD_INPUT_HANDLE");
  }
  return (intptr_t) ret;
}

intptr_t
os_terminal_get_stdout_handle(void) {
  HANDLE ret = GetStdHandle(STD_OUTPUT_HANDLE);
  if (ret == INVALID_HANDLE_VALUE) {
    util_bail("can't get STD_OUTPUT_HANDLE");
  }
  return (intptr_t) ret;
}

void
os_terminal_setup(intptr_t handle) {
  (void) handle;
}

int
os_terminal_has_readable_bytes(intptr_t handle) {
  DWORD ret = WaitForSingleObject((HANDLE) handle, 1);
  if (ret == WAIT_TIMEOUT) {
    return 0;
  } else if (ret == WAIT_OBJECT_0) {
    return 1;
  }
  util_bail("WaitForSingleObject failed");
  return 0;
}

int
os_terminal_handle_read_byte(intptr_t handle, uint8_t* p_byte) {
  DWORD read = 0;
  BOOL ret = ReadFile((HANDLE) handle, p_byte, 1, &read, NULL);
  if (!ret || (read != 1)) {
    return 0;
  }
  return 1;
}

int
os_terminal_handle_write_byte(intptr_t handle, uint8_t byte) {
  DWORD written = 0;
  BOOL ret = WriteFile((HANDLE) handle, &byte, 1, &written, NULL);
  if (!ret || (written != 1)) {
    return 0;
  }
  return 1;
}
