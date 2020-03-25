#include "os_alloc.h"

#include "util.h"

#include <windows.h>

struct os_alloc_mapping {
  void* p_addr;
  size_t size;
  int is_file;
};

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
  BOOL ret = CloseHandle((HANDLE) h);
  if (ret == 0) {
    util_bail("CloseHandle failed");
  }
}

void*
os_alloc_get_mapping_addr(struct os_alloc_mapping* p_mapping) {
  return p_mapping->p_addr;
}

static struct os_alloc_mapping*
os_alloc_get_mapping_from_handle(intptr_t h,
                                 void* p_addr,
                                 size_t size,
                                 int fixed) {
  LPVOID p_map;

  HANDLE handle = (HANDLE) h;
  struct os_alloc_mapping* p_ret =
      util_mallocz(sizeof(struct os_alloc_mapping));

  (void) fixed;

  if (handle != NULL) {
    p_ret->is_file = 1;
    p_map = MapViewOfFileEx(handle,
                            FILE_MAP_WRITE,
                            0,
                            0,
                            size,
                            p_addr);
    if (p_map == NULL) {
      util_bail("MapViewOfFileEx failed");
    }
  } else {
    p_ret->is_file = 0;
    p_map = VirtualAlloc(p_addr,
                         size,
                         (MEM_RESERVE | MEM_COMMIT),
                         PAGE_READWRITE);
    if (p_map == NULL) {
      util_bail("VirtualAlloc failed");
    }
  }

  p_ret->p_addr = p_map;
  p_ret->size = size;

  if ((p_addr != NULL) && (p_map != p_addr)) {
    util_bail("VirtualAlloc address mismatch");
  }

  return p_ret;
}

void
os_alloc_get_mapping_from_handle_replace(intptr_t handle,
                                         void* p_addr,
                                         size_t size) {
  (void) handle;
  (void) p_addr;
  (void) size;
}

struct os_alloc_mapping*
os_alloc_get_guarded_mapping_from_handle(intptr_t handle,
                                         void* p_addr,
                                         size_t size) {
  return os_alloc_get_mapping_from_handle(handle, p_addr, size, 0);
}

struct os_alloc_mapping*
os_alloc_get_guarded_mapping(void* p_addr, size_t size) {
  return os_alloc_get_guarded_mapping_from_handle((intptr_t) NULL,
                                                  p_addr,
                                                  size);
}

void
os_alloc_get_anonymous_mapping_replace(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
}

void
os_alloc_free_mapping(struct os_alloc_mapping* p_mapping) {
  BOOL ret;
  void* p_addr = p_mapping->p_addr;

  if (p_mapping->is_file) {
    ret = UnmapViewOfFile(p_addr);
    if (ret == 0) {
      util_bail("UnmapViewOfFile failed");
    }
  } else {
    ret = VirtualFree(p_addr, 0, MEM_RELEASE);
    if (ret == 0) {
      util_bail("VirtualFree failed");
    }
  }

  util_free(p_mapping);
}

void
os_alloc_make_mapping_read_only(void* p_addr, size_t size) {
  DWORD old_protection;
  BOOL ret = VirtualProtect(p_addr, size, PAGE_READONLY, &old_protection);
  if (ret == 0) {
    util_bail("VirtualProtect PAGE_READONLY failed");
  }
}

void
os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size) {
  DWORD old_protection;
  BOOL ret = VirtualProtect(p_addr,
                            size,
                            PAGE_EXECUTE_READWRITE,
                            &old_protection);
  if (ret == 0) {
    util_bail("VirtualProtect PAGE_EXECUTE_READWRITE failed");
  }
}

void
os_alloc_make_mapping_none(void* p_addr, size_t size) {
  DWORD old_protection;
  BOOL ret = VirtualProtect(p_addr, size, PAGE_NOACCESS, &old_protection);
  if (ret == 0) {
    util_bail("VirtualProtect PAGE_NOACCESS failed");
  }
}
