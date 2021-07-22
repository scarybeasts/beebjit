#ifndef BEEBJIT_OS_ALLOC_H
#define BEEBJIT_OS_ALLOC_H

#include <stddef.h>
#include <stdint.h>

struct os_alloc_mapping;

int os_alloc_get_is_64k_mappings(void);

void* os_alloc_get_aligned(size_t alignment, size_t size);
void os_alloc_free_aligned(void* p);

intptr_t os_alloc_get_memory_handle(size_t size);
void os_alloc_free_memory_handle(intptr_t handle);

void* os_alloc_get_mapping_addr(struct os_alloc_mapping* p_mapping);
struct os_alloc_mapping* os_alloc_get_mapping_from_handle(intptr_t handle,
                                                          void* p_addr,
                                                          size_t offset,
                                                          size_t size);
struct os_alloc_mapping* os_alloc_get_mapping(void* p_addr, size_t size);

void os_alloc_free_mapping(struct os_alloc_mapping* p_mapping);

void os_alloc_make_mapping_read_only(void* p_addr, size_t size);
void os_alloc_make_mapping_read_write(void* p_addr, size_t size);
void os_alloc_make_mapping_read_write_exec(void* p_addr, size_t size);
void os_alloc_make_mapping_read_exec(void* p_addr, size_t size);
void os_alloc_make_mapping_none(void* p_addr, size_t size);

#endif /* BEEBJIT_OS_ALLOC_H */
