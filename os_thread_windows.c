#include "os_thread.h"

struct os_thread_struct*
os_thread_create(void* p_func, void* p_arg) {
  (void) p_func;
  (void) p_arg;
  return NULL;
}

intptr_t
os_thread_destroy(struct os_thread_struct* p_thread_struct) {
  (void) p_thread_struct;
  return 0;
}

struct os_lock_struct*
os_lock_create() {
  return NULL;
}

void
os_lock_destroy(struct os_lock_struct* p_lock) {
  (void) p_lock;
}

void
os_lock_lock(struct os_lock_struct* p_lock) {
  (void) p_lock;
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  (void) p_lock;
}
