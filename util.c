#include "util.h"

#include <assert.h>
#include <err.h>

#include <sys/mman.h>

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
