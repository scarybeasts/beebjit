#ifndef BEEBJIT_OS_FAULT_PLATFORM_H
#define BEEBJIT_OS_FAULT_PLATFORM_H

#include <stdint.h>

int os_fault_is_write_fault(void* p_context);
int os_fault_is_exec_fault(void* p_context);
uintptr_t os_fault_get_jit_context(void* p_context);

uintptr_t os_fault_get_pc(void* p_context);
void os_fault_set_pc(void* p_context, uintptr_t pc);

int os_fault_is_carry_flag_set(uintptr_t host_flags);
int os_fault_is_overflow_flag_set(uintptr_t host_flags);

#endif /* BEEBJIT_OS_FAULT_PLATFORM_H */
