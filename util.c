#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

void*
util_mallocz(size_t size) {
  void* p_ret = malloc(size);
  if (p_ret == NULL) {
    util_bail("malloc failed");
  }

  (void) memset(p_ret, '\0', size);

  return p_ret;
}

void
util_free(void* p) {
  free(p);
}

struct util_buffer {
  uint8_t* p_mem;
  size_t length;
  size_t pos;
  uint8_t* p_base;
};

struct util_buffer*
util_buffer_create() {
  struct util_buffer* p_buf = util_mallocz(sizeof(struct util_buffer));

  return p_buf;
}

void
util_buffer_destroy(struct util_buffer* p_buf) {
  free(p_buf);
}

void
util_buffer_setup(struct util_buffer* p_buf, uint8_t* p_mem, size_t len) {
  p_buf->p_mem = p_mem;
  p_buf->length = len;
  p_buf->pos = 0;
  p_buf->p_base = p_mem;
}

uint8_t*
util_buffer_get_ptr(struct util_buffer* p_buf) {
  return p_buf->p_mem;
}

size_t
util_buffer_get_pos(struct util_buffer* p_buf) {
  return p_buf->pos;
}

void
util_buffer_set_pos(struct util_buffer* p_buf, size_t pos) {
  assert(pos <= p_buf->length);
  p_buf->pos = pos;
}

size_t
util_buffer_remaining(struct util_buffer* p_buf) {
  return (p_buf->length - p_buf->pos);
}

void
util_buffer_append(struct util_buffer* p_buf, struct util_buffer* p_src_buf) {
  assert((p_buf->pos + p_src_buf->pos) >= p_buf->pos);
  assert((p_buf->pos + p_src_buf->pos) <= p_buf->length);

  memcpy((p_buf->p_mem + p_buf->pos), p_src_buf->p_mem, p_src_buf->pos);
  p_buf->pos += p_src_buf->pos;
}

void
util_buffer_set_base_address(struct util_buffer* p_buf, uint8_t* p_base) {
  p_buf->p_base = p_base;
}

uint8_t*
util_buffer_get_base_address(struct util_buffer* p_buf) {
  return p_buf->p_base;
}

void
util_buffer_add_1b(struct util_buffer* p_buf, int b1) {
  assert(b1 >= 0 && b1 <= 0xff);
  assert(p_buf->pos < p_buf->length);
  p_buf->p_mem[p_buf->pos++] = b1;
}

void
util_buffer_add_2b(struct util_buffer* p_buf, int b1, int b2) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
}

void
util_buffer_add_2b_1w(struct util_buffer* p_buf, int b1, int b2, int w1) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, w1 & 0xff);
  util_buffer_add_1b(p_buf, w1 >> 8);
}

void
util_buffer_add_3b(struct util_buffer* p_buf, int b1, int b2, int b3) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
}

void
util_buffer_add_4b(struct util_buffer* p_buf, int b1, int b2, int b3, int b4) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
  util_buffer_add_1b(p_buf, b4);
}

void
util_buffer_add_5b(struct util_buffer* p_buf,
                   int b1,
                   int b2,
                   int b3,
                   int b4,
                   int b5) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
  util_buffer_add_1b(p_buf, b4);
  util_buffer_add_1b(p_buf, b5);
}

void
util_buffer_add_int(struct util_buffer* p_buf, int64_t i) {
  int b1;
  int b2;
  int b3;
  int b4;
  assert(i >= INT_MIN);
  assert(i <= INT_MAX);
  b1 = (i & 0xff);
  i >>= 8;
  b2 = (i & 0xff);
  i >>= 8;
  b3 = (i & 0xff);
  i >>= 8;
  b4 = (i & 0xff);

  util_buffer_add_4b(p_buf, b1, b2, b3, b4);
}

void
util_buffer_add_chunk(struct util_buffer* p_buf, void* p_src, size_t size) {
  assert((p_buf->pos + size) >= p_buf->pos);
  assert((p_buf->pos + size) <= p_buf->length);
  (void) memcpy((p_buf->p_mem + p_buf->pos), p_src, size);
  p_buf->pos += size;
}

void
util_buffer_fill_to_end(struct util_buffer* p_buf, char value) {
  util_buffer_fill(p_buf, value, (p_buf->length - p_buf->pos));
}

void
util_buffer_fill(struct util_buffer* p_buf, char value, size_t len) {
  assert((p_buf->pos + len) >= p_buf->pos);
  assert((p_buf->pos + len) <= p_buf->length);
  (void) memset((p_buf->p_mem + p_buf->pos), value, len);
  p_buf->pos += len;
}

int
util_is_extension(const char* p_file_name, const char* p_ext) {
  const char* p_file_name_end;
  const char* p_ext_end;
  size_t i;

  size_t file_name_len = strlen(p_file_name);
  size_t ext_len = strlen(p_ext);

  if (file_name_len < (ext_len + 1)) {
    return 0;
  }

  p_file_name_end = (p_file_name + file_name_len);
  p_ext_end = (p_ext + ext_len);

  i = 0;
  while (i < ext_len) {
    p_file_name_end--;
    p_ext_end--;
    if (*p_ext_end != tolower(*p_file_name_end)) {
      return 0;
    }
    ++i;
  }

  p_file_name_end--;
  if (*p_file_name_end != '.') {
    return 0;
  }

  return 1;
}

intptr_t
util_file_handle_open(const char* p_file_name, int writeable, int create) {
  int flags;
  int fd;

  if (writeable) {
    flags = O_RDWR;
    if (create) {
      flags |= O_CREAT;
    }
  } else {
    flags = O_RDONLY;
  }

  fd = open(p_file_name, flags, 0600);
  if (fd < 0) {
    util_bail("couldn't open %s", p_file_name);
  }

  return (intptr_t) fd;
}

void
util_file_handle_close(intptr_t handle) {
  int fd = (int) handle;
  int ret = close(fd);
  if (ret != 0) {
    util_bail("close failed");
  }
}

uint64_t
util_file_handle_get_size(intptr_t handle) {
  struct stat stat_buf;

  int fd = (int) handle;
  int ret = fstat(fd, &stat_buf);
  if (ret != 0 || stat_buf.st_size < 0) {
    util_bail("fstat failed");
  }

  return stat_buf.st_size;
}

void
util_file_handle_seek(intptr_t handle, uint64_t pos) {
  off_t ret;

  int fd = (int) handle;

  ret = lseek(fd, (off_t) pos, SEEK_SET);
  if ((uint64_t) ret != pos) {
    util_bail("lseek failed");
  }
}

void
util_file_handle_write(intptr_t handle, const void* p_buf, uint64_t length) {
  int fd = (int) handle;
  uint64_t to_go = length;

  while (to_go > 0) {
    ssize_t ret = write(fd, p_buf, to_go);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      util_bail("write failed");
    } else if (ret == 0) {
      util_bail("write EOF");
    }
    to_go -= ret;
    p_buf += ret;
  }
}

uint64_t
util_file_handle_read(intptr_t handle, void* p_buf, uint64_t length) {
  int fd = (int) handle;
  uint64_t to_go = length;
  uint64_t done = 0;

  while (to_go > 0) {
    ssize_t ret = read(fd, p_buf, to_go);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      util_bail("read failed");
    } else if (ret == 0) {
      break;
    }
    to_go -= ret;
    done += ret;
    p_buf += ret;
  }

  return done;
}

uint64_t
util_file_read_fully(uint8_t* p_buf,
                     uint64_t max_size,
                     const char* p_file_name) {
  size_t read_ret;
  intptr_t handle = util_file_handle_open(p_file_name, 0, 0);

  read_ret = util_file_handle_read(handle, p_buf, max_size);

  util_file_handle_close(handle);

  return read_ret;
}

void
util_file_write_fully(const char* p_file_name,
                      const uint8_t* p_buf,
                      uint64_t size) {
  intptr_t handle = util_file_handle_open(p_file_name, 1, 1);
  util_file_handle_write(handle, p_buf, size);
  util_file_handle_close(handle);
}

intptr_t
util_get_stdin_handle() {
  return fileno(stdin);
}

intptr_t
util_get_stdout_handle() {
  return fileno(stdout);
}

uint8_t
util_handle_read_byte(intptr_t handle) {
  uint8_t val;

  ssize_t ret = read(handle, &val, 1);
  if (ret != 1) {
    util_bail("failed to read byte from handle");
  }

  return val;
}

void
util_handle_write_byte(intptr_t handle, uint8_t val) {
  ssize_t ret = write(handle, &val, 1);
  if (ret != 1) {
    util_bail("failed to write byte to handle");
  }
}

static const char*
util_locate_option(const char* p_opt_str,
                   const char* p_opt_name) {
  const char* p_opt_pos;

  if (p_opt_str == NULL) {
    return NULL;
  }

  p_opt_pos = strstr(p_opt_str, p_opt_name);
  if (p_opt_pos != NULL) {
    p_opt_pos += strlen(p_opt_name);
  }

  return p_opt_pos;
}

int
util_get_u32_option(uint32_t* p_opt_out,
                    const char* p_opt_str,
                    const char* p_opt_name) {
  int matches;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  matches = sscanf(p_opt_pos, "%u", p_opt_out);
  if (matches != 1) {
    return 0;
  }
  return 1;
}

int
util_get_str_option(char** p_opt_out,
                    const char* p_opt_str,
                    const char* p_opt_name) {
  size_t len;
  const char* p_opt_end;
  char* p_ret;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  p_opt_end = p_opt_pos;
  while (*p_opt_end != '\0' && *p_opt_end != ',') {
    p_opt_end++;
  }

  /* NOTE: would use strndup() here but Windows doesn't have it verbatim. */
  len = (p_opt_end - p_opt_pos);
  p_ret = malloc(len + 1);
  (void) memcpy(p_ret, p_opt_pos, len);
  p_ret[len] = '\0';
  *p_opt_out = p_ret;

  return 1;
}

int
util_has_option(const char* p_opt_str, const char* p_opt_name) {
  if (strstr(p_opt_str, p_opt_name)) {
    return 1;
  }
  return 0;
}

void
util_bail(const char* p_msg, ...) {
  va_list args;
  char msg[256];

  va_start(args, p_msg);
  msg[0] = '\0';
  (void) vsnprintf(msg, sizeof(msg), p_msg, args);
  va_end(args);

  (void) fprintf(stderr, "BAILING: %s\n", msg);

  exit(1);
  /* Not reached. */
}
