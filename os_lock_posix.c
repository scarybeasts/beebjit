#include "os_lock.h"

#include "util.h"

#include <pthread.h>

struct os_lock_struct {
  pthread_spinlock_t lock;
};

struct os_lock_struct*
os_lock_create() {
  int ret;
  struct os_lock_struct* p_lock = util_mallocz(sizeof(struct os_lock_struct));

  ret = pthread_spin_init(&p_lock->lock, PTHREAD_PROCESS_PRIVATE);
  if (ret != 0) {
    errx(1, "pthread_spin_init failed");
  }

  return p_lock;
}

void
os_lock_destroy(struct os_lock_struct* p_lock) {
  int ret = pthread_spin_destroy(&p_lock->lock);
  if (ret != 0) {
    errx(1, "pthread_spin_destroy failed");
  }
  util_free(p_lock);
}

void
os_lock_lock(struct os_lock_struct* p_lock) {
  int ret = pthread_spin_lock(&p_lock->lock);
  if (ret != 0) {
    errx(1, "pthread_spin_lock failed");
  }
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  int ret = pthread_spin_unlock(&p_lock->lock);
  if (ret != 0) {
    errx(1, "pthread_spin_unlock failed");
  }
}
