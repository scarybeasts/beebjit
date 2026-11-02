#include "os_fault_platform.h"

#include "asm/asm_defs_host.h"

#include <ucontext.h>

int
os_fault_is_write_fault(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  uintptr_t reg_err = p_context->uc_mcontext.gregs[REG_ERR];
  return !!(reg_err & 2);
}

int
os_fault_is_exec_fault(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  uintptr_t reg_err = p_context->uc_mcontext.gregs[REG_ERR];
  return !!(reg_err & 16);
}

uintptr_t
os_fault_get_jit_context(void* p) {
  ucontext_t* p_context = (ucontext_t*) p;
  /* REG_CONTEXT is biased, see K_CONTEXT_REG_OFFSET. */
  return (p_context->uc_mcontext.gregs[REG_RDI] - K_CONTEXT_REG_OFFSET);
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

void
os_fault_set_trap_param(void* p, uintptr_t param) {
  ucontext_t* p_context = (ucontext_t*) p;
  p_context->uc_mcontext.gregs[REG_RSI] = param;
}

int
os_fault_is_carry_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0001);
}

int
os_fault_is_overflow_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0800);
}
