#include "os_thread.h"

#include <err.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// https://github.com/majek/dump/blob/master/msqueue/pthread_spin_lock_shim.h

struct os_lock_struct {
  int value;
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
  struct os_lock_struct* p_lock = malloc(sizeof(struct os_lock_struct));
  if (p_lock == NULL) {
    errx(1, "couldn't allocate os_lock_struct");
  }

  __asm__ __volatile__ ("" ::: "memory");
  p_lock->value = 0;

  return p_lock;
}

void
os_lock_destroy(struct os_lock_struct* p_lock) {
  free(p_lock);
}

void
os_lock_lock(struct os_lock_struct* p_lock) {
  while (1) {
    int i;
    for (i=0; i < 10000; i++) {
      if (__sync_bool_compare_and_swap(&p_lock->value, 0, 1)) {
        return;
      }
    }
    sched_yield();
  }
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  __asm__ __volatile__ ("" ::: "memory");
  p_lock->value = 0;
}
