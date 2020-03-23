#include "os_thread.h"

#include "util.h"

struct os_thread_struct {
  HANDLE handle;
  void* p_func;
  void* p_arg;
  void* p_ret;
};

DWORD WINAPI
ThreadProc(_In_ LPVOID lpParameter) {
  void* p_ret;
  struct os_thread_struct* p_thread = (struct os_thread_struct*) lpParameter;

  void* (*p_callback)(void*) = p_thread->p_func;

  p_ret = p_callback(p_thread->p_arg);

  p_thread->p_ret = p_ret;

  return (DWORD) (intptr_t) p_ret;
}

struct os_thread_struct*
os_thread_create(void* p_func, void* p_arg) {
  HANDLE handle;
  struct os_thread_struct* p_thread =
      util_mallocz(sizeof(struct os_thread_struct));

  p_thread->p_func = p_func;
  p_thread->p_arg = p_arg;

  handle = CreateThread(NULL, 0, ThreadProc, p_thread, 0, NULL);
  if (handle == NULL) {
    util_bail("CreateThread failed");
  }
  p_thread->handle = handle;

  return p_thread;
}

intptr_t
os_thread_destroy(struct os_thread_struct* p_thread_struct) {
  (void) p_thread_struct;
  util_bail("os_thread_destroy");
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
  util_bail("os_lock_lock");
}

void
os_lock_unlock(struct os_lock_struct* p_lock) {
  (void) p_lock;
}
