#include "os_poller.h"

#include "util.h"

#include <assert.h>
#include <windows.h>

enum {
  k_os_poller_max_handles = 2,
};

struct os_poller_struct {
  HANDLE handles[k_os_poller_max_handles];
  uint32_t num_used_handles;
  int32_t triggered_handle;
};

struct os_poller_struct*
os_poller_create() {
  struct os_poller_struct* p_poller =
      util_mallocz(sizeof(struct os_poller_struct));

  p_poller->triggered_handle = -1;

  return p_poller;
}

void
os_poller_destroy(struct os_poller_struct* p_poller) {
  util_free(p_poller);
}

void
os_poller_add_handle(struct os_poller_struct* p_poller, intptr_t handle) {
  uint32_t num_used_handles = p_poller->num_used_handles;

  if (num_used_handles == k_os_poller_max_handles) {
    util_bail("os_poller_add_handle out of handles");
  }

  p_poller->handles[num_used_handles] = (HANDLE) handle;
  p_poller->num_used_handles++;
}

void
os_poller_poll(struct os_poller_struct* p_poller) {
  uint32_t num_used_handles = p_poller->num_used_handles;

  if ((num_used_handles == 0) ||
      (p_poller->handles[num_used_handles - 1] != NULL)) {
    util_bail("missing NULL (the window handle)");
  }

  /* NOTE: needs to be QS_ALLINPUT and not QS_ALLEVENTS otherwise there's a
   * strange hang trying to restore the minimized window.
   */
  DWORD ret = MsgWaitForMultipleObjects((num_used_handles - 1),
                                        p_poller->handles,
                                        FALSE,
                                        INFINITE,
                                        QS_ALLINPUT);
  if (ret == WAIT_FAILED) {
    util_bail("MsgWaitForMultipleObjects failed");
  }

  p_poller->triggered_handle = (ret - WAIT_OBJECT_0);
}

int
os_poller_handle_triggered(struct os_poller_struct* p_poller, uint32_t index) {
  int32_t triggered_handle = p_poller->triggered_handle;
  assert(triggered_handle != -1);
  return (index == (uint32_t) triggered_handle);
}
