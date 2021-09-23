#include "os_fault_platform.h"

#include <ucontext.h>

int
os_fault_is_write_fault(void* p) {
  (void) p;
  /* Unknown -- seems hard to directly get at on ARM64.
   * It looks like you can walk uc_mcontext.__reserved[4096] and look for a
   * chunk of ESR_MAGIC to get ESR_EL1. However, that register doesn't
   * obviously differentiate between read and write permission faults.
   *
   * Other code bases read the 4 byte instruction at PC, since ARM64
   * instructions decode easily.
   */
  return -1;
}

int
os_fault_is_exec_fault(void* p) {
  (void) p;
  /* Unknown. */
  return -1;
}

uintptr_t
os_fault_get_jit_context(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext.regs[25];
}

uintptr_t
os_fault_get_pc(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext.pc;
}

void
os_fault_set_pc(void* p, uintptr_t pc) {
  ucontext_t* p_context = (ucontext_t*) p;
  p_context->uc_mcontext.pc = pc;
}
