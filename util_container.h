#ifndef BEEBJIT_UTIL_CONTAINER_H
#define BEEBJIT_UTIL_CONTAINER_H

#include <stdint.h>

struct util_list_struct;

struct util_list_struct* util_list_alloc(void);
void util_list_free(struct util_list_struct* p_list);
void util_list_clear(struct util_list_struct* p_list);

uint32_t util_list_get_count(struct util_list_struct* p_list);
intptr_t util_list_get(struct util_list_struct* p_list, uint32_t index);

intptr_t util_list_set(struct util_list_struct* p_list,
                       uint32_t index,
                       intptr_t item);
void util_list_add(struct util_list_struct* p_list, intptr_t item);
void util_list_insert(struct util_list_struct* p_list,
                      uint32_t index,
                      intptr_t item);
void util_list_append_list(struct util_list_struct* p_list,
                           struct util_list_struct* p_src_list);
intptr_t util_list_remove(struct util_list_struct* p_list, uint32_t index);

#endif /* BEEBJIT_UTIL_CONTAINER_H */
