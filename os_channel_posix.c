#include "os_channel.h"

#include "util.h"

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>

void
os_channel_get_handles(intptr_t* p_read1,
                       intptr_t* p_write1,
                       intptr_t* p_read2,
                       intptr_t* p_write2) {
  int fds[2];
  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
  if (ret != 0) {
    util_bail("socketpair failed");
  }

  *p_read1 = fds[0];
  *p_write1 = fds[1];
  *p_read2 = fds[1];
  *p_write2 = fds[0];
}

void
os_channel_free_handles(intptr_t read1,
                        intptr_t write1,
                        intptr_t read2,
                        intptr_t write2) {
  int ret;

  assert(read1 == write2);
  assert(write1 == read2);
  (void) read2;
  (void) write2;

  ret = close((int) read1);
  if (ret != 0) {
    util_bail("close failed");
  }
  ret = close((int) write1);
  if (ret != 0) {
    util_bail("close failed");
  }
}

void
os_channel_read(intptr_t handle, void* p_buf, uint32_t length) {
  int fd = (int) handle;
  uint64_t to_go = length;

  while (to_go > 0) {
    ssize_t ret = read(fd, p_buf, to_go);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      util_bail("os_channel_read failed");
    } else if (ret == 0) {
      util_bail("os_channel_read EOF");
    }
    to_go -= ret;
    p_buf = ((uint8_t*) p_buf + ret);
  }
}

void
os_channel_write(intptr_t handle, const void* p_buf, uint32_t length) {
  int fd = (int) handle;
  uint64_t to_go = length;

  while (to_go > 0) {
    ssize_t ret = write(fd, p_buf, to_go);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      util_bail("os_channel_write failed");
    } else if (ret == 0) {
      util_bail("os_channel_write EOF");
    }
    to_go -= ret;
    p_buf = ((uint8_t*) p_buf + ret);
  }
}
