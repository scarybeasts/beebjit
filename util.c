#include "util.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>

static const size_t k_guard_size = 4096;

struct util_file_map {
  void* p_mmap;
  size_t length;
};

int
util_get_memory_fd(size_t size) {
  int ret;
  int fd = syscall(SYS_memfd_create, "beebjit", 0);
  if (fd < 0) {
    errx(1, "memfd_create failed");
  }

  ret = ftruncate(fd, size);
  if (ret != 0) {
    errx(1, "ftruncate failed");
  }

  return fd;
}

static void*
util_get_mapping_from_fd(int fd, void* p_addr, size_t size, int fixed) {
  int map_flags;
  void* p_map;

  if (fd == -1) {
    map_flags = MAP_PRIVATE | MAP_ANONYMOUS;
  } else {
    map_flags = MAP_SHARED;
  }
  if (fixed) {
    map_flags |= MAP_FIXED;
  }

  p_map = mmap(p_addr, size, PROT_READ | PROT_WRITE, map_flags, fd, 0);
  if (p_map == MAP_FAILED) {
    errx(1, "mmap failed");
  }

  if (p_addr != NULL && p_map != p_addr) {
    errx(1, "mmap in wrong location");
  }

  return p_map;
}

void*
util_get_fixed_mapping_from_fd(int fd, void* p_addr, size_t size) {
  return util_get_mapping_from_fd(fd, p_addr, size, 1);
}

void*
util_get_guarded_mapping(void* p_addr, size_t size) {
  return util_get_guarded_mapping_from_fd(-1, p_addr, size);
}

void*
util_get_guarded_mapping_from_fd(int fd, void* p_addr, size_t size) {
  void* p_map;
  void* p_guard;

  assert(size + (k_guard_size * 2) > size);

  p_map = util_get_mapping_from_fd(fd, p_addr, size, 0);

  p_guard = mmap(p_map - k_guard_size,
                 k_guard_size,
                 PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0);
  if (p_guard == MAP_FAILED) {
    errx(1, "mmap failed");
  }

  if (p_guard != p_map - k_guard_size) {
    errx(1, "mmap in wrong location");
  }

  p_guard = mmap(p_map + size,
                 k_guard_size,
                 PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0);
  if (p_guard == MAP_FAILED) {
    errx(1, "mmap failed");
  }

  if (p_guard != p_map + size) {
    errx(1, "mmap in wrong location");
  }

  return p_map;
}

void*
util_get_fixed_anonymous_mapping(void* p_addr, size_t size) {
  void* p_map = mmap(p_addr,
                     size,
                     PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                     -1,
                     0);
  if (p_map == MAP_FAILED) {
    errx(1, "mmap failed");
  }

  if (p_map != p_addr) {
    errx(1, "mmap in wrong location");
  }

  return p_map;
}

void
util_free_guarded_mapping(void* p_addr, size_t size) {
  char* p_map = p_addr - k_guard_size;
  size += (k_guard_size * 2);
  int ret = munmap(p_map, size);
  if (ret != 0) {
    errx(1, "munmap failed");
  }
}

void
util_make_mapping_read_only(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ);
  if (ret != 0) {
    errx(1, "mprotect failed");
  }
}

void
util_make_mapping_read_write(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ|PROT_WRITE);
  if (ret != 0) {
    errx(1, "mprotect failed");
  }
}

void
util_make_mapping_read_write_exec(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ|PROT_WRITE|PROT_EXEC);
  if (ret != 0) {
    errx(1, "mprotect failed");
  }
}

void
util_make_mapping_none(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect failed");
  }
}

struct util_buffer {
  uint8_t* p_mem;
  size_t length;
  size_t pos;
  uint8_t* p_base;
};

struct util_buffer*
util_buffer_create() {
  struct util_buffer* p_buf = malloc(sizeof(struct util_buffer));
  if (p_buf == NULL) {
    errx(1, "couldn't allocate util_buffer");
  }
  p_buf->p_mem = NULL;
  p_buf->length = 0;
  p_buf->pos = 0;
  p_buf->p_base = NULL;

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
util_buffer_add_int(struct util_buffer* p_buf, ssize_t i) {
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
    errx(1, "couldn't open %s", p_file_name);
  }

  return (intptr_t) fd;
}

void
util_file_handle_close(intptr_t handle) {
  int fd = (int) handle;
  int ret = close(fd);
  if (ret != 0) {
    errx(1, "close failed");
  }
}

uint64_t
util_file_handle_get_size(intptr_t handle) {
  struct stat stat_buf;

  int fd = (int) handle;
  int ret = fstat(fd, &stat_buf);
  if (ret != 0 || stat_buf.st_size < 0) {
    errx(1, "fstat failed");
  }

  return stat_buf.st_size;
}

void
util_file_handle_write(intptr_t handle, const void* p_buf, size_t length) {
  /* TODO: handle short writes here and below. */
  int fd = (int) handle;
  ssize_t ret = write(fd, p_buf, length);
  if ((ret < 0) || ((uint64_t) ret != length)) {
    errx(1, "write failed");
  }
}

size_t
util_file_handle_read(intptr_t handle, void* p_buf, size_t length) {
  int fd = (int) handle;
  ssize_t ret = read(fd, p_buf, length);
  if (ret < 0) {
    errx(1, "read failed");
  }
  return (size_t) ret;
}

size_t
util_file_read_fully(uint8_t* p_buf, size_t max_size, const char* p_file_name) {
  size_t read_ret;
  intptr_t handle = util_file_handle_open(p_file_name, 0, 0);

  read_ret = util_file_handle_read(handle, p_buf, max_size);

  util_file_handle_close(handle);

  return read_ret;
}

void
util_file_write_fully(const char* p_file_name,
                      const uint8_t* p_buf,
                      size_t size) {
  intptr_t handle = util_file_handle_open(p_file_name, 1, 1);
  util_file_handle_write(handle, p_buf, size);
  util_file_handle_close(handle);
}

struct util_file_map*
util_file_map(const char* p_file_name, size_t max_length, int writeable) {
  struct util_file_map* p_map;
  int mmap_prot;
  void* p_mmap;
  int64_t handle;
  uint64_t size;

  if (writeable) {
    mmap_prot = (PROT_READ | PROT_WRITE);
  } else {
    mmap_prot = PROT_READ;
  }

  p_map = malloc(sizeof(struct util_file_map));
  if (p_map == NULL) {
    errx(1, "couldn't allocate util_file_map");
  }
  (void) memset(p_map, '\0', sizeof(struct util_file_map));

  handle = util_file_handle_open(p_file_name, writeable, 0);
  size = util_file_handle_get_size(handle);

  if (size > max_length) {
    errx(1, "file too large");
  }

  p_mmap = mmap(NULL, size, mmap_prot, MAP_SHARED, (int) handle, 0);
  if (p_map == MAP_FAILED) {
    errx(1, "mmap failed");
  }

  p_map->p_mmap = p_mmap;
  p_map->length = size;

  util_file_handle_close(handle);

  return p_map;
}

void*
util_file_map_get_ptr(struct util_file_map* p_map) {
  return p_map->p_mmap;
}

size_t
util_file_map_get_size(struct util_file_map* p_map) {
  return p_map->length;
}

void
util_file_unmap(struct util_file_map* p_map) {
  int ret = munmap(p_map->p_mmap, p_map->length);
  if (ret != 0) {
    errx(1, "munmap failed");
  }

  free(p_map);
}

uint64_t
util_gettime_us() {
  struct timespec ts;

  int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret != 0) {
    errx(1, "clock_gettime failed");
  }

  return ((ts.tv_sec * (uint64_t) 1000000) + (ts.tv_nsec / 1000));
}

void
util_sleep_us(uint64_t us) {
  int ret;
  struct timespec ts;

  ts.tv_sec = (us / 1000000);
  ts.tv_nsec = ((us % 1000000) * 1000);

  do {
    ret = nanosleep(&ts, &ts);
    if (ret != 0) {
      if (errno == EINTR) {
        continue;
      }
      errx(1, "nanosleep failed");
    }
  } while (ret != 0);
}

void
util_get_channel_fds(int* fd1, int* fd2) {
  int fds[2];
  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
  if (ret != 0) {
    errx(1, "socketpair failed");
  }

  *fd1 = fds[0];
  *fd2 = fds[1];
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
  const char* p_opt_end;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  p_opt_end = p_opt_pos;
  while (*p_opt_end != '\0' && *p_opt_end != ',') {
    p_opt_end++;
  }

  *p_opt_out = strndup(p_opt_pos, (p_opt_end - p_opt_pos));

  return 1;
}

int
util_has_option(const char* p_opt_str, const char* p_opt_name) {
  if (strstr(p_opt_str, p_opt_name)) {
    return 1;
  }
  return 0;
}
