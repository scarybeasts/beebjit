#include "os_lock.h"

#include "util.h"

#include <os/lock.h>

struct os_lock_struct {
  os_unfair_lock lock;
};

struct os_lock_struct*
os_lock_create() {
  struct os_lock_struct* p_lock = util_mallocz(sizeof(struct os_lock_struct));
  p_lock->lock = OS_UNFAIR_LOCK_INIT;
  return p_lock;
}

void
os_lock_destroy(struct os_lock_struct* p_lock) {
  util_free(p_lock);
}

void
os_lock_lock(struct os_lock_struct* p_lock) {
  os_unfair_lock_lock(&p_lock->lock);
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  os_unfair_lock_unlock(&p_lock->lock);
}
