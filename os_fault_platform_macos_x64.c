#include "os_fault_platform.h"

#include <sys/ucontext.h>

int
os_fault_is_write_fault(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  uintptr_t reg_err = p_context->uc_mcontext->__es.__err;
  return !!(reg_err & 2);
}

int
os_fault_is_exec_fault(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  uintptr_t reg_err = p_context->uc_mcontext->__es.__err;
  return !!(reg_err & 16);
}

uintptr_t
os_fault_get_jit_context(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext->__ss.__rdi;
}

uintptr_t
os_fault_get_pc(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  return p_context->uc_mcontext->__ss.__rip;
}

void
os_fault_set_pc(void* p, uintptr_t pc) {
  ucontext_t* p_context = (ucontext_t*) p;
  p_context->uc_mcontext->__ss.__rip = pc;
}

int
os_fault_is_carry_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0001);
}

int
os_fault_is_overflow_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0800);
}
