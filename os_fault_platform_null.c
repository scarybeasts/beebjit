#include "os_fault_platform.h"

uintptr_t
os_fault_get_eflags(void* p) {
  (void) p;
  return 0;
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
