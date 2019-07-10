#include "os_poller.h"

#include <assert.h>
#include <errno.h>
#include <err.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_os_poller_max_handles = 2,
};

struct os_poller_struct {
  struct pollfd poll_fds[k_os_poller_max_handles];
  size_t num_used_handles;
};

struct os_poller_struct*
os_poller_create() {
  struct os_poller_struct* p_poller = malloc(sizeof(struct os_poller_struct));
  if (p_poller == NULL) {
    errx(1, "couldn't allocate os_poller_struct");
  }

  (void) memset(p_poller, '\0', sizeof(struct os_poller_struct));

  p_poller->num_used_handles = 0;

  return p_poller;
}

void
os_poller_destroy(struct os_poller_struct* p_poller) {
  free(p_poller);
}

void
os_poller_add_handle(struct os_poller_struct* p_poller, size_t handle) {
  size_t num_used_handles = p_poller->num_used_handles;

  if (p_poller->num_used_handles == k_os_poller_max_handles) {
    errx(1, "os_poller_add_handle out of handles");
  }

  p_poller->poll_fds[num_used_handles].fd = handle;
  p_poller->poll_fds[num_used_handles].events = POLLIN;
  p_poller->poll_fds[num_used_handles].revents = 0;

  p_poller->num_used_handles++;
}

void
os_poller_poll(struct os_poller_struct* p_poller) {
  size_t i;
  int ret;

  size_t num_used_handles = p_poller->num_used_handles;

  for (i = 0; i < num_used_handles; ++i) {
    p_poller->poll_fds[i].revents = 0;
  }

  while (1) {
    ret = poll(&p_poller->poll_fds[0], num_used_handles, -1);
    if (ret > 0) {
      return;
    } else if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      errx(1, "poll failed");
    } else {
      assert(0);
    }
  }
}

int
os_poller_handle_triggered(struct os_poller_struct* p_poller, size_t index) {
  assert(index < p_poller->num_used_handles);

  return (p_poller->poll_fds[index].revents & POLLIN);
}
