#include "os_lock.h"

#include "util.h"

#include <windows.h>

struct os_lock_struct {
  CRITICAL_SECTION cs;
};

struct os_lock_struct*
os_lock_create() {
  struct os_lock_struct* p_lock = util_mallocz(sizeof(struct os_lock_struct));

  InitializeCriticalSection(&p_lock->cs);

  return p_lock;
}

void
os_lock_destroy(struct os_lock_struct* p_lock) {
  DeleteCriticalSection(&p_lock->cs);
  util_free(p_lock);
}

void
os_lock_lock(struct os_lock_struct* p_lock) {
  EnterCriticalSection(&p_lock->cs);
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  LeaveCriticalSection(&p_lock->cs);
}
