#include "os_fault.h"

#include "util.h"

#include <windows.h>

#include <stdio.h>

static os_fault_callback_t s_p_fault_callback;

static LONG
VectoredHandler(struct _EXCEPTION_POINTERS* p_info) {
  EXCEPTION_RECORD* p_record = p_info->ExceptionRecord;
  CONTEXT* p_context = p_info->ContextRecord;
  int handled;
  int is_exec = 0;
  int is_write = 0;
  int is_trap = 0;
  uintptr_t host_fault_addr;

  if (p_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    DWORD64 flags = p_record->ExceptionInformation[0];
    host_fault_addr = (uintptr_t) p_record->ExceptionInformation[1];
    is_exec = !!(flags & 8);
    is_write = !!(flags & 1);
  } else if (p_record->ExceptionCode == EXCEPTION_BREAKPOINT) {
    /* Windows winds Rip back to the int3 itself; the callback contract is the
     * POSIX one, pc past the trap instruction.
     */
    is_trap = 1;
    host_fault_addr = 0;
    p_context->Rip += 1;
  } else {
    os_fault_bail();
  }

  handled = s_p_fault_callback((uintptr_t*) &p_context->Rip,
                               host_fault_addr,
                               0,
                               is_exec,
                               is_write,
                               is_trap,
                               (uintptr_t) p_context->Rdi);
  if (!handled) {
    os_fault_bail();
  }

  return EXCEPTION_CONTINUE_EXECUTION;
}

void
os_fault_register_handler(os_fault_callback_t p_fault_callback) {
  void* p_ret;

  s_p_fault_callback = p_fault_callback;

  /* 1 is for CALL_FIRST. */
  p_ret = AddVectoredExceptionHandler(1, VectoredHandler);
  if (p_ret == NULL) {
    util_bail("AddVectoredExceptionHandler failed");
  }
}

void
os_fault_bail(void) {
  HANDLE process = GetCurrentProcess();
  while (1) {
    (void) TerminateProcess(process, 1);
  }
}

void
os_debug_trap(void) {
  DebugBreak();
}

int
os_fault_is_carry_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0001);
}

int
os_fault_is_overflow_flag_set(uintptr_t host_flags) {
  return !!(host_flags & 0x0800);
}
