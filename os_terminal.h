#ifndef BEEBJIT_OS_TERMINAL_H
#define BEEBJIT_OS_TERMINAL_H

#include <stdint.h>

intptr_t os_terminal_get_stdin_handle(void);
intptr_t os_terminal_get_stdout_handle(void);
void os_terminal_setup(intptr_t handle);
int os_terminal_has_readable_bytes(intptr_t handle);
int os_terminal_handle_read_byte(intptr_t handle, uint8_t* p_byte);
int os_terminal_handle_write_byte(intptr_t handle, uint8_t byte);

#endif /* BEEBJIT_OS_TERMINAL_H */
