#include "os_fault_platform.h"

int
os_fault_is_write_fault(void* p) {
  (void) p;
  return -1;
}

int
os_fault_is_exec_fault(void* p) {
  (void) p;
  return -1;
}

uintptr_t
os_fault_get_jit_context(void* p) {
  (void) p;
  return 0;
}

uintptr_t
os_fault_get_pc(void* p) {
  (void) p;
  return 0;
}

void
os_fault_set_pc(void* p, uintptr_t pc) {
  (void) p;
  (void) pc;
}

int
os_fault_is_carry_flag_set(uintptr_t host_flags) {
  (void) host_flags;
  return 0;
}

int
os_fault_is_overflow_flag_set(uintptr_t host_flags) {
  (void) host_flags;
  return 0;
}
