#include "os_alloc.h"

#include "util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

static const size_t k_guard_size = 4096;

void*
os_alloc_get_aligned(size_t alignment, size_t size) {
  int ret;
  void* p_alloc = NULL;

  ret = posix_memalign(&p_alloc, alignment, size);
  if (ret != 0) {
    util_bail("posix_memalign failed");
  }

  return p_alloc;
}

void
os_alloc_free_aligned(void* p) {
  free(p);
}

intptr_t
os_alloc_get_memory_handle(size_t size) {
  int fd;
  int ret;
  char file_name[19];

  (void) strcpy(file_name, "/tmp/beebjitXXXXXX");
  fd = mkstemp(file_name);
  if (fd < 0) {
    util_bail("mkstemp failed");
  }

  ret = unlink(file_name);
  if (ret != 0) {
    util_bail("unlink failed");
  }

  ret = ftruncate(fd, size);
  if (ret != 0) {
    util_bail("ftruncate failed");
  }

  return fd;
}

void
os_alloc_free_memory_handle(intptr_t h) {
  int fd = (int) h;
  int ret = close(fd);
  if (ret != 0) {
    util_bail("close failed");
  }
}

static void*
os_alloc_get_mapping_from_handle(intptr_t handle,
                                 void* p_addr,
                                 size_t size,
                                 int fixed) {
  int map_flags;
  void* p_map;

  if (handle == -1) {
    map_flags = (MAP_PRIVATE | MAP_ANONYMOUS);
  } else {
    map_flags = MAP_SHARED;
  }
  if (fixed) {
    map_flags |= MAP_FIXED;
  }

  p_map = mmap(p_addr, size, (PROT_READ | PROT_WRITE), map_flags, handle, 0);
  if (p_map == MAP_FAILED) {
    util_bail("mmap failed");
  }

  if ((p_addr != NULL) && (p_map != p_addr)) {
    util_bail("mmap in wrong location");
  }

  return p_map;
}

void
os_alloc_get_mapping_from_handle_replace(intptr_t handle,
                                         void* p_addr,
                                         size_t size) {
  (void) os_alloc_get_mapping_from_handle(handle, p_addr, size, 1);
}

void*
os_alloc_get_guarded_mapping_from_handle(intptr_t handle,
                                         void* p_addr,
                                         size_t size) {
  void* p_map;
  void* p_guard;

  assert((size + (k_guard_size * 2)) > size);

  p_map = os_alloc_get_mapping_from_handle(handle, p_addr, size, 0);

  p_guard = mmap((p_map - k_guard_size),
                 k_guard_size,
                 PROT_NONE,
                 (MAP_PRIVATE | MAP_ANONYMOUS),
                 -1,
                 0);
  if (p_guard == MAP_FAILED) {
    util_bail("mmap failed");
  }

  if (p_guard != (p_map - k_guard_size)) {
    util_bail("mmap in wrong location");
  }

  p_guard = mmap((p_map + size),
                 k_guard_size,
                 PROT_NONE,
                 (MAP_PRIVATE | MAP_ANONYMOUS),
                 -1,
                 0);
  if (p_guard == MAP_FAILED) {
    util_bail("mmap failed");
  }

  if (p_guard != (p_map + size)) {
    util_bail("mmap in wrong location");
  }

  return p_map;
}

void*
os_alloc_get_guarded_mapping(void* p_addr, size_t size) {
  return os_alloc_get_guarded_mapping_from_handle(-1, p_addr, size);
}

void
os_alloc_free_guarded_mapping(void* p_addr, size_t size) {
  uint8_t* p_map = (p_addr - k_guard_size);
  size += (k_guard_size * 2);
  int ret = munmap(p_map, size);
  if (ret != 0) {
    util_bail("munmap failed");
  }
}

void
os_alloc_get_anonymous_mapping_replace(void* p_addr, size_t size) {
  void* p_map = mmap(p_addr,
                     size,
                     (PROT_READ | PROT_WRITE),
                     (MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS),
                     -1,
                     0);
  if (p_map == MAP_FAILED) {
    util_bail("mmap failed");
  }

  if (p_map != p_addr) {
    util_bail("mmap in wrong location");
  }
}

void
os_alloc_make_mapping_read_only(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ);
  if (ret != 0) {
    util_bail("mprotect failed");
  }
}

void
os_alloc_make_mapping_read_write(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, (PROT_READ | PROT_WRITE));
  if (ret != 0) {
    util_bail("mprotect failed");
  }
}

void
os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, (PROT_READ | PROT_WRITE | PROT_EXEC));
  if (ret != 0) {
    util_bail("mprotect failed");
  }
}

void
os_alloc_make_mapping_none(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_NONE);
  if (ret != 0) {
    util_bail("mprotect failed");
  }
}
