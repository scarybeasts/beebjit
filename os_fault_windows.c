#include "os_fault.h"

#include "util.h"

#include <windows.h>

#include <stdio.h>

static void (*s_p_fault_callback)(uintptr_t*, uintptr_t, int, int, uintptr_t);

static LONG
VectoredHandler(struct _EXCEPTION_POINTERS* p_info) {
  EXCEPTION_RECORD* p_record = p_info->ExceptionRecord;
  CONTEXT* p_context = p_info->ContextRecord;
  DWORD64 flags = p_record->ExceptionInformation[0];

  if (p_record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    os_fault_bail();
  }

  s_p_fault_callback((uintptr_t*) &p_context->Rip,
                     (uintptr_t) p_record->ExceptionInformation[1],
                     !!(flags & 8),
                     !!(flags & 1),
                     (uintptr_t) p_context->Rdi);

  return EXCEPTION_CONTINUE_EXECUTION;
}

void
os_fault_register_handler(
    void (*p_fault_callback)(uintptr_t* p_host_rip,
                             uintptr_t host_fault_addr,
                             int is_exec,
                             int is_write,
                             uintptr_t host_rdi)) {
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
