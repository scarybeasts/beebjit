#include "os_alloc.h"

#include "util.h"

#include <stdlib.h>

void*
os_alloc_aligned(size_t alignment, size_t size) {
  int ret;
  void* p_alloc = NULL;

  ret = posix_memalign(&p_alloc, alignment, size);
  if (ret != 0) {
    util_bail("posix_memalign failed");
  }

  return p_alloc;
}
