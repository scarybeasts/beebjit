#ifndef BEEBJIT_OS_FAULT_H
#define BEEBJIT_OS_FAULT_H

#include <stdint.h>

void os_fault_register_handler(
    void (*p_fault_callback)(uintptr_t* p_host_rip,
                             uintptr_t host_fault_addr,
                             uintptr_t host_exception_flags,
                             uintptr_t host_rdi));
void os_fault_bail(void);

#endif /* BEEBJIT_OS_FAULT_H */
