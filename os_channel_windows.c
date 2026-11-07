#include "os_channel.h"

#include "util.h"

#include "os_lock.h"

#include <assert.h>
#include <windows.h>

/* Both ends of a channel direction share one of these. Anonymous pipes can't
 * be waited on, so the event is kept set while the pipe has data, and that's
 * what the poller waits on. The lock makes writing and setting the event
 * atomic with respect to the reader checking whether to reset it.
 */
struct os_channel_pipe {
  HANDLE handle_read;
  HANDLE handle_write;
  HANDLE handle_event;
  struct os_lock_struct* p_lock;
};

static struct os_channel_pipe*
os_channel_pipe_create() {
  BOOL ret;
  struct os_channel_pipe* p_pipe =
      util_mallocz(sizeof(struct os_channel_pipe));

  ret = CreatePipe(&p_pipe->handle_read, &p_pipe->handle_write, NULL, 0);
  if (ret == 0) {
    util_bail("CreatePipe failed");
  }
  /* Manual reset, initially not set. */
  p_pipe->handle_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (p_pipe->handle_event == NULL) {
    util_bail("CreateEvent failed");
  }
  p_pipe->p_lock = os_lock_create();

  return p_pipe;
}

static void
os_channel_pipe_destroy(struct os_channel_pipe* p_pipe) {
  BOOL ret;
  ret = CloseHandle(p_pipe->handle_read);
  if (ret == 0) {
    util_bail("CloseHandle failed");
  }
  ret = CloseHandle(p_pipe->handle_write);
  if (ret == 0) {
    util_bail("CloseHandle failed");
  }
  ret = CloseHandle(p_pipe->handle_event);
  if (ret == 0) {
    util_bail("CloseHandle failed");
  }
  os_lock_destroy(p_pipe->p_lock);
  util_free(p_pipe);
}

void
os_channel_get_handles(intptr_t* p_read1,
                       intptr_t* p_write1,
                       intptr_t* p_read2,
                       intptr_t* p_write2) {
  struct os_channel_pipe* p_pipe1 = os_channel_pipe_create();
  struct os_channel_pipe* p_pipe2 = os_channel_pipe_create();

  *p_read1 = (intptr_t) p_pipe1;
  *p_write1 = (intptr_t) p_pipe1;
  *p_read2 = (intptr_t) p_pipe2;
  *p_write2 = (intptr_t) p_pipe2;
}

void
os_channel_free_handles(intptr_t read1,
                        intptr_t write1,
                        intptr_t read2,
                        intptr_t write2) {
  assert(read1 == write1);
  assert(read2 == write2);
  (void) write1;
  (void) write2;

  os_channel_pipe_destroy((struct os_channel_pipe*) read1);
  os_channel_pipe_destroy((struct os_channel_pipe*) read2);
}

intptr_t
os_channel_get_poll_handle(intptr_t handle) {
  struct os_channel_pipe* p_pipe = (struct os_channel_pipe*) handle;
  return (intptr_t) p_pipe->handle_event;
}

void
os_channel_read(intptr_t handle, void* p_message, uint32_t length) {
  DWORD bytes_read;
  DWORD bytes_available;
  struct os_channel_pipe* p_pipe = (struct os_channel_pipe*) handle;

  BOOL ret = ReadFile(p_pipe->handle_read,
                      p_message,
                      length,
                      &bytes_read,
                      NULL);
  if (ret == 0) {
    util_bail("ReadFile failed");
  }
  if (bytes_read != length) {
    util_bail("ReadFile short read");
  }

  os_lock_lock(p_pipe->p_lock);
  ret = PeekNamedPipe(p_pipe->handle_read,
                      NULL,
                      0,
                      NULL,
                      &bytes_available,
                      NULL);
  if (ret == 0) {
    util_bail("PeekNamedPipe failed");
  }
  if (bytes_available == 0) {
    ret = ResetEvent(p_pipe->handle_event);
    if (ret == 0) {
      util_bail("ResetEvent failed");
    }
  }
  os_lock_unlock(p_pipe->p_lock);
}

void
os_channel_write(intptr_t handle, const void* p_message, uint32_t length) {
  DWORD bytes_written;
  struct os_channel_pipe* p_pipe = (struct os_channel_pipe*) handle;
  BOOL ret;

  os_lock_lock(p_pipe->p_lock);
  ret = WriteFile(p_pipe->handle_write,
                  p_message,
                  length,
                  &bytes_written,
                  NULL);
  if (ret == 0) {
    util_bail("WriteFile failed");
  }
  if (bytes_written != length) {
    util_bail("WriteFile short write");
  }
  ret = SetEvent(p_pipe->handle_event);
  if (ret == 0) {
    util_bail("SetEvent failed");
  }
  os_lock_unlock(p_pipe->p_lock);
}
