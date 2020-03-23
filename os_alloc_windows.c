#include "os_alloc.h"

#include "util.h"

#include <windows.h>

void*
os_alloc_get_aligned(size_t alignment, size_t size) {
  void* p_ret = _aligned_malloc(size, alignment);
  if (p_ret == NULL) {
    util_bail("_aligned_malloc failed");
  }

  return p_ret;
}

void
os_alloc_free_aligned(void* p) {
  _aligned_free(p);
}

intptr_t
os_alloc_get_memory_handle(size_t size) {
  HANDLE ret = CreateFileMapping(INVALID_HANDLE_VALUE,
                                 NULL,
                                 PAGE_READWRITE,
                                 (size >> 32),
                                 (size & 0xffffffff),
                                 NULL);
  return (intptr_t) ret;
}

void
os_alloc_free_memory_handle(intptr_t h) {
  (void) h;
  util_bail("blah");
}

static void*
os_alloc_get_mapping_from_handle(intptr_t h,
                                 void* p_addr,
                                 size_t size,
                                 int fixed) {
  LPVOID p_ret;
  HANDLE handle = (HANDLE) h;

  (void) fixed;

  if (handle != NULL) {
    p_ret = MapViewOfFileEx(handle,
                           FILE_MAP_WRITE,
                           0,
                           0,
                           size,
                           p_addr);
    if (p_ret == NULL) {
      util_bail("MapViewOfFileEx failed");
    }
  } else {
    p_ret = VirtualAlloc(p_addr,
                        size,
                        (MEM_RESERVE | MEM_COMMIT),
                        PAGE_READWRITE);
    if (p_ret == NULL) {
      util_bail("VirtualAlloc failed");
    }
  }

  if ((p_addr != NULL) && (p_ret != p_addr)) {
    util_bail("VirtualAlloc address mismatch");
  }

  return p_ret;
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
  return os_alloc_get_mapping_from_handle(handle, p_addr, size, 0);
}

void*
os_alloc_get_guarded_mapping(void* p_addr, size_t size) {
  return os_alloc_get_guarded_mapping_from_handle((intptr_t) NULL,
                                                  p_addr,
                                                  size);
}

void
os_alloc_free_guarded_mapping(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
  util_bail("blah");
}

void
os_alloc_get_anonymous_mapping_replace(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}

void
os_alloc_make_mapping_read_only(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}

void
os_alloc_make_mapping_read_write(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}

void
os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}

void
os_alloc_make_mapping_none(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}
