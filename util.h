#ifndef BEEBJIT_UTIL_H
#define BEEBJIT_UTIL_H

#include <stddef.h>

void* util_get_guarded_mapping(void* p_addr, size_t size, int is_exec);
void util_free_guarded_mapping(void* p_addr, size_t size);
void util_make_mapping_read_only(void* p_addr, size_t size);

#endif /* BEEBJIT_UTIL_H */
