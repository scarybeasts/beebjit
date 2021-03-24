#ifndef BEEBJIT_UTIL_STRING_H
#define BEEBJIT_UTIL_STRING_H

#include <stdint.h>

struct util_string_list_struct;

struct util_string_list_struct* util_string_list_alloc(void);
void util_string_list_free(struct util_string_list_struct* p_list);
void util_string_list_clear(struct util_string_list_struct* p_list);
uint32_t util_string_list_get_count(struct util_string_list_struct* p_list);
const char* util_string_list_get_string(struct util_string_list_struct* p_list,
                                        uint32_t i);

void util_string_list_set_at_with_length(struct util_string_list_struct* p_list,
                                         uint32_t index,
                                         const char* p_str,
                                         uint32_t len);
void util_string_list_set_at(struct util_string_list_struct* p_list,
                             uint32_t index,
                             const char* p_str);
void util_string_list_add_with_length(struct util_string_list_struct* p_list,
                                      const char* p_str,
                                      uint32_t len);
void util_string_list_add(struct util_string_list_struct* p_list,
                          const char* p_str);
void util_string_list_insert(struct util_string_list_struct* p_list,
                             uint32_t index,
                             const char* p_str);
void util_string_list_append_list(struct util_string_list_struct* p_list,
                                  struct util_string_list_struct* p_src_list);
void util_string_list_remove(struct util_string_list_struct* p_list,
                             uint32_t index);

void util_string_split(struct util_string_list_struct* p_list,
                       const char* p_str,
                       char split_char,
                       char quote_char);

#endif /* BEEBJIT_UTIL_STRING_H */
