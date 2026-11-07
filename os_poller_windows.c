#include "os_poller.h"

#include "util.h"

#include <windows.h>

enum {
  k_os_poller_max_handles = 2,
};

struct os_poller_struct {
  HANDLE handles[k_os_poller_max_handles];
  uint32_t num_used_handles;
  uint32_t triggered_mask;
};

struct os_poller_struct*
os_poller_create() {
  struct os_poller_struct* p_poller =
      util_mallocz(sizeof(struct os_poller_struct));

  return p_poller;
}

void
os_poller_destroy(struct os_poller_struct* p_poller) {
  util_free(p_poller);
}

void
os_poller_add_handle(struct os_poller_struct* p_poller, intptr_t handle) {
  uint32_t num_used_handles;

  /* NULL is the window handle. It is automatically included in the Windows
   * wait call, so we can ignore it.
   */
  if (handle == (intptr_t) NULL) {
    return;
  }

  num_used_handles = p_poller->num_used_handles;
  if (num_used_handles == k_os_poller_max_handles) {
    util_bail("os_poller_add_handle out of handles");
  }

  p_poller->handles[num_used_handles] = (HANDLE) handle;
  p_poller->num_used_handles++;
}

void
os_poller_poll(struct os_poller_struct* p_poller) {
  uint32_t i;
  DWORD ret;
  uint32_t num_used_handles = p_poller->num_used_handles;
  uint32_t triggered_mask = 0;

  /* NOTE: needs to be QS_ALLINPUT and not QS_ALLEVENTS otherwise there's a
   * strange hang trying to restore the minimized window.
   */
  ret = MsgWaitForMultipleObjects(num_used_handles,
                                  p_poller->handles,
                                  FALSE,
                                  INFINITE,
                                  QS_ALLINPUT);
  if (ret == WAIT_FAILED) {
    util_bail("MsgWaitForMultipleObjects failed");
  }

  /* The wait only returns the first ready source, so check them all, like
   * poll(), otherwise a busy channel could starve the window. The handles are
   * manual reset events, so checking them doesn't change them.
   */
  for (i = 0; i < num_used_handles; ++i) {
    if (WaitForSingleObject(p_poller->handles[i], 0) == WAIT_OBJECT_0) {
      triggered_mask |= (1 << i);
    }
  }
  /* The window's message queue is the index after the handles. */
  if (ret == (WAIT_OBJECT_0 + num_used_handles)) {
    triggered_mask |= (1 << num_used_handles);
  } else if (MsgWaitForMultipleObjectsEx(0,
                                         NULL,
                                         0,
                                         QS_ALLINPUT,
                                         MWMO_INPUTAVAILABLE) ==
             WAIT_OBJECT_0) {
    triggered_mask |= (1 << num_used_handles);
  }

  p_poller->triggered_mask = triggered_mask;
}

int
os_poller_handle_triggered(struct os_poller_struct* p_poller, uint32_t index) {
  return !!(p_poller->triggered_mask & (1 << index));
}
