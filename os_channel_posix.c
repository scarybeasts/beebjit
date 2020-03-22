#include "os_channel.h"

#include "util.h"

#include <sys/socket.h>

void
os_channel_get_handles(intptr_t* p_handle1, intptr_t* p_handle2) {
  int fds[2];
  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
  if (ret != 0) {
    util_bail("socketpair failed");
  }

  *p_handle1 = fds[0];
  *p_handle2 = fds[1];
}
