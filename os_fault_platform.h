#ifndef BEEBJIT_OS_FAULT_PLATFORM_H
#define BEEBJIT_OS_FAULT_PLATFORM_H

#include <stdint.h>

uintptr_t os_fault_get_eflags(void* p_context);
uintptr_t os_fault_get_jit_context(void* p_context);

uintptr_t os_fault_get_pc(void* p_context);
void os_fault_set_pc(void* p_context, uintptr_t pc);

#endif /* BEEBJIT_OS_FAULT_PLATFORM_H */
