#ifndef BEEBJIT_OS_FAULT_H
#define BEEBJIT_OS_FAULT_H

#include <stdint.h>

/* The fault callback. is_trap is set for a software breakpoint (x64: int3)
 * rather than a memory fault; *p_host_pc is then the address after the trap
 * instruction, on all platforms. Returns non-zero if handled, in which case
 * execution resumes at *p_host_pc. Returns zero if not recognized: the OS
 * layer bails.
 */
typedef int (*os_fault_callback_t)(uintptr_t* p_host_pc,
                                   uintptr_t host_fault_addr,
                                   int is_illegal,
                                   int is_exec,
                                   int is_write,
                                   int is_trap,
                                   uintptr_t host_context);

void os_fault_register_handler(os_fault_callback_t p_fault_callback);
void os_fault_bail(void);

void os_debug_trap(void);

#endif /* BEEBJIT_OS_FAULT_H */
