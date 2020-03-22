#include "os_poller.h"

struct os_poller_struct*
os_poller_create() {
  return NULL;
}

void
os_poller_destroy(struct os_poller_struct* p_poller) {
  (void) p_poller;
}

void
os_poller_add_handle(struct os_poller_struct* p_poller, uintptr_t handle) {
  (void) p_poller;
  (void) handle;
}

void
os_poller_poll(struct os_poller_struct* p_poller) {
  (void) p_poller;
}

int
os_poller_handle_triggered(struct os_poller_struct* p_poller, uint32_t index) {
  (void) p_poller;
  (void) index;
  return 0;
}
