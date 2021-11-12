#include "os_thread.h"

#include "util.h"

#include <pthread.h>

struct os_thread_struct {
  pthread_t thread;
};

struct os_thread_struct*
os_thread_create(void* p_func, void* p_arg) {
  int ret;
  struct os_thread_struct* p_thread =
      util_mallocz(sizeof(struct os_thread_struct));

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
  util_free(p_thread_struct);

  return (intptr_t) p_retval;
}
