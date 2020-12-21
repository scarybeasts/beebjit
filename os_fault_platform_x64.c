#include "os_fault_platform.h"

#include <ucontext.h>

uintptr_t
os_fault_get_eflags(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext.gregs[REG_ERR];
}

uintptr_t
os_fault_get_jit_context(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext.gregs[REG_RDI];
}

uintptr_t
os_fault_get_pc(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext.gregs[REG_RIP];
}

void
os_fault_set_pc(void* p, uintptr_t pc) {
  ucontext_t* p_context = (ucontext_t*) p;
  p_context->uc_mcontext.gregs[REG_RIP] = pc;
}
