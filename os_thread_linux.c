#include "os_thread.h"

#include <err.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct os_lock_struct {
  pthread_spinlock_t lock;
};

struct os_thread_struct {
  pthread_t thread;
};

struct os_thread_struct*
os_thread_create(void* p_func, void* p_arg) {
  int ret;
  struct os_thread_struct* p_thread = malloc(sizeof(struct os_thread_struct));
  if (p_thread == NULL) {
    errx(1, "couldn't allocate os_thread_struct");
  }

  (void) memset(p_thread, '\0', sizeof(struct os_thread_struct));

  ret = pthread_create(&p_thread->thread, NULL, p_func, p_arg);
  if (ret != 0) {
    errx(1, "pthread_create failed");
  }

  return p_thread;
}

intptr_t
os_thread_destroy(struct os_thread_struct* p_thread_struct) {
  void* p_retval;
  int ret = pthread_join(p_thread_struct->thread, &p_retval);
  if (ret != 0) {
    errx(1, "pthread_join failed");
  }
  free(p_thread_struct);

  return (intptr_t) p_retval;
}

struct os_lock_struct*
os_lock_create() {
  int ret;
  struct os_lock_struct* p_lock = malloc(sizeof(struct os_lock_struct));
  if (p_lock == NULL) {
    errx(1, "couldn't allocate os_lock_struct");
  }

  (void) memset(p_lock, '\0', sizeof(struct os_lock_struct));

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
  free(p_lock);
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
