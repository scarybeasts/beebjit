#include "os_alloc.h"

#include "log.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

struct os_alloc_mapping {
  void* p_addr;
  size_t size;
};

int
os_alloc_get_is_64k_mappings(void) {
  return 0;
}

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

void*
os_alloc_get_mapping_addr(struct os_alloc_mapping* p_mapping) {
  return p_mapping->p_addr;
}

struct os_alloc_mapping*
os_alloc_get_mapping_from_handle(intptr_t handle,
                                 void* p_addr,
                                 size_t offset,
                                 size_t size) {
  void* p_map;
  int map_flags = 0;
  int try_huge = 0;
  int map_prot = (PROT_READ | PROT_WRITE);

  struct os_alloc_mapping* p_ret =
      util_mallocz(sizeof(struct os_alloc_mapping));

  (void) try_huge;

/* macOS lacks MAP_HUGETLB. */
#ifdef MAP_HUGETLB
  if ((size % (2 * 1024 * 1024)) == 0) {
    try_huge = 1;
    map_flags |= MAP_HUGETLB;
  }
#endif

  if (handle == -1) {
    map_flags |= (MAP_PRIVATE | MAP_ANONYMOUS);
  } else {
    map_flags |= MAP_SHARED;
  }

  p_map = mmap(p_addr, size, map_prot, map_flags, handle, offset);
/* macOS lacks MAP_HUGETLB. */
#ifdef MAP_HUGETLB
  if (try_huge) {
    if (p_map == MAP_FAILED) {
      map_flags &= ~MAP_HUGETLB;
      p_map = mmap(p_addr, size, map_prot, map_flags, handle, offset);
    } else {
      log_do_log(k_log_misc, k_log_info, "used MAP_HUGETLB");
    }
  }
#endif
  if (p_map == MAP_FAILED) {
    util_bail("mmap failed");
  }

  if ((p_addr != NULL) && (p_map != p_addr)) {
    util_bail("mmap in wrong location (expected %p, got %p)"
              ", heap %p binary %p",
              p_addr,
              p_map,
              p_ret,
              os_alloc_get_mapping_from_handle);
  }

  p_ret->p_addr = p_map;
  p_ret->size = size;

  return p_ret;
}

struct os_alloc_mapping*
os_alloc_get_mapping(void* p_addr, size_t size) {
  return os_alloc_get_mapping_from_handle(-1, p_addr, 0, size);
}

void
os_alloc_free_mapping(struct os_alloc_mapping* p_mapping) {
  int ret;

  void* p_addr = p_mapping->p_addr;
  size_t size = p_mapping->size;

  ret = munmap(p_addr, size);
  if (ret != 0) {
    util_bail("munmap failed");
  }

  util_free(p_mapping);
}

void
os_alloc_make_mapping_read_only(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_READ);
  if (ret != 0) {
    util_bail("mprotect R failed @%p", p_addr);
  }
}

void
os_alloc_make_mapping_read_write(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, (PROT_READ | PROT_WRITE));
  if (ret != 0) {
    util_bail("mprotect RW failed @%p", p_addr);
  }
}

void
os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, (PROT_READ | PROT_WRITE | PROT_EXEC));
  if (ret != 0) {
    util_bail("mprotect RWX failed @%p", p_addr);
  }
}

void
os_alloc_make_mapping_read_exec(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, (PROT_READ | PROT_EXEC));
  if (ret != 0) {
    util_bail("mprotect RX failed @%p", p_addr);
  }
}

void
os_alloc_make_mapping_none(void* p_addr, size_t size) {
  int ret = mprotect(p_addr, size, PROT_NONE);
  if (ret != 0) {
    util_bail("mprotect N failed @%p", p_addr);
  }
}
