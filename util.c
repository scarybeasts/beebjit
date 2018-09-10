#include "util.h"

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

static const size_t k_guard_size = 4096;

void*
util_get_guarded_mapping(void* p_addr, size_t size, int is_exec) {
  void* p_map;
  int prot;
  int ret;

  assert(size + (k_guard_size * 2) > size);

  size += (k_guard_size * 2);
  p_addr -= k_guard_size;

  prot  = PROT_READ | PROT_WRITE;
  if (is_exec) {
    prot |= PROT_EXEC;
  }

  p_map = mmap(p_addr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p_map == MAP_FAILED) {
    errx(1, "mmap() failed");
  }

  if (p_addr != NULL && p_map != p_addr) {
    errx(1, "mmap() in wrong location");
  }

  ret = mprotect(p_map, k_guard_size, PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }

  ret = mprotect(p_map + size - k_guard_size, k_guard_size, PROT_NONE);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }

  return p_map + k_guard_size;
}

void
util_free_guarded_mapping(void* p_addr, size_t size) {
  char* p_map = p_addr - k_guard_size;
  size += (k_guard_size * 2);
  int ret = munmap(p_map, size);
  if (ret != 0) {
    errx(1, "munmap() failed");
  }
}

void
util_make_mapping_read_only(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ);
  if (ret != 0) {
    errx(1, "mprotect() failed");
  }
}

struct util_buffer {
  unsigned char* p_mem;
  size_t length;
  size_t pos;
  unsigned char* p_base;
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
util_buffer_setup(struct util_buffer* p_buf, unsigned char* p_mem, size_t len) {
  p_buf->p_mem = p_mem;
  p_buf->length = len;
  p_buf->pos = 0;
  p_buf->p_base = NULL;
}

unsigned char*
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
  return p_buf->length - p_buf->pos;
}

void
util_buffer_append(struct util_buffer* p_buf, struct util_buffer* p_src_buf) {
  assert(p_buf->pos + p_src_buf->pos >= p_buf->pos);
  assert(p_buf->pos + p_src_buf->pos <= p_buf->length);

  memcpy(p_buf->p_mem + p_buf->pos, p_src_buf->p_mem, p_src_buf->pos);
  p_buf->pos += p_src_buf->pos;
}

void
util_buffer_set_base_address(struct util_buffer* p_buf, unsigned char* p_base) {
  p_buf->p_base = p_base;
}

unsigned char*
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

size_t
util_read_file(unsigned char* p_buf, size_t max_size, const char* p_file_name) {
  int ret;
  ssize_t read_ret;

  int fd = open(p_file_name, O_RDONLY);
  if (fd < 0) {
    errx(1, "open failed");
  }

  read_ret = read(fd, p_buf, max_size);
  if (read_ret < 0) {
    errx(1, "read failed");
  }

  ret = close(fd);
  if (ret != 0) {
    errx(1, "close failed");
  }

  return read_ret;
}

void
util_write_file(const char* p_file_name,
                const unsigned char* p_buf,
                size_t size) {
  int ret;
  ssize_t write_ret;

  int fd = open(p_file_name, O_WRONLY | O_CREAT, 0600);
  if (fd < 0) {
    errx(1, "open failed");
  }

  write_ret = write(fd, p_buf, size);
  if (write_ret != size) {
    errx(1, "write failed");
  }

  ret = close(fd);
  if (ret != 0) {
    errx(1, "close failed");
  }
}
