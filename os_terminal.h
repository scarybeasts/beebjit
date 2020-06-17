#ifndef BEEBJIT_OS_TERMINAL_H
#define BEEBJIT_OS_TERMINAL_H

#include <stdint.h>

void os_terminal_setup(intptr_t handle);
uint64_t os_terminal_readable_bytes(intptr_t handle);

#endif /* BEEBJIT_OS_TERMINAL_H */
