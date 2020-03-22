#include "os_alloc.h"

void*
os_alloc_aligned(size_t alignment, size_t size) {
  (void) alignment;
  (void) size;
  return NULL;
}

intptr_t
os_alloc_get_memory_handle(size_t size) {
  (void) size;
  return -1;
}

static void*
os_alloc_get_mapping_from_handle(intptr_t handle,
                                 void* p_addr,
                                 size_t size,
                                 int fixed) {
  (void) handle;
  (void) p_addr;
  (void) size;
  (void) fixed;
  return NULL;
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
  (void) handle;
  (void) p_addr;
  (void) size;
  return NULL;
}

void*
os_alloc_get_guarded_mapping(void* p_addr, size_t size) {
  return os_alloc_get_guarded_mapping_from_handle(-1, p_addr, size);
}

void
os_alloc_free_guarded_mapping(void* p_addr, size_t size) {
  (void) p_addr;
  (void) size;
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
