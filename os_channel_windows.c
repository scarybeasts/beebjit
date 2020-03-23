#include "os_channel.h"

#include "util.h"

#include <windows.h>

void
os_channel_get_handles(intptr_t* p_read1,
                       intptr_t* p_write1,
                       intptr_t* p_read2,
                       intptr_t* p_write2) {
  BOOL ret;
  HANDLE handle_pipe_read;
  HANDLE handle_pipe_write;

  ret = CreatePipe(&handle_pipe_read, &handle_pipe_write, NULL, 0);
  if (ret == 0) {
    util_bail("CreatePipe failed");
  }
  *p_read1 = (intptr_t) handle_pipe_read;
  *p_write1 = (intptr_t) handle_pipe_write;

  ret = CreatePipe(&handle_pipe_read, &handle_pipe_write, NULL, 0);
  if (ret == 0) {
    util_bail("CreatePipe failed");
  }
  *p_read2 = (intptr_t) handle_pipe_read;
  *p_write2 = (intptr_t) handle_pipe_write;
}

void
os_channel_free_handles(intptr_t read1,
                        intptr_t write1,
                        intptr_t read2,
                        intptr_t write2) {
  (void) read1;
  (void) write1;
  (void) read2;
  (void) write2;
  util_bail("blah");
}

void
os_channel_read(intptr_t h, void* p_message, uint32_t length) {
  DWORD bytes_read;
  HANDLE handle = (HANDLE) h;

  BOOL ret = ReadFile(handle, p_message, length, &bytes_read, NULL);
  if (ret == 0) {
    util_bail("ReadFile failed");
  }
  if (bytes_read != length) {
    util_bail("ReadFile short read");
  }
}

void
os_channel_write(intptr_t h, const void* p_message, uint32_t length) {
  DWORD bytes_written;
  HANDLE handle = (HANDLE) h;

  BOOL ret = WriteFile(handle, p_message, length, &bytes_written, NULL);
  if (ret == 0) {
    util_bail("WriteFile failed");
  }
  if (bytes_written != length) {
    util_bail("WriteFile short write");
  }
}
