#ifndef BEEBJIT_UTIL_STRING_H
#define BEEBJIT_UTIL_STRING_H

#include <stdint.h>

struct util_string_list_struct;

struct util_string_list_struct* util_string_list_alloc(void);
void util_string_list_free(struct util_string_list_struct* p_list);
void util_string_list_clear(struct util_string_list_struct* p_list);
uint32_t util_string_list_get_count(struct util_string_list_struct* p_list);
char* util_string_list_get_string(struct util_string_list_struct* p_list,
                                  uint32_t i);
int util_string_list_get_int(struct util_string_list_struct* p_list,
                             int64_t p_out_val,
                             uint32_t i);
int util_string_list_get_hex_int(struct util_string_list_struct* p_list,
                                 int64_t p_out_val,
                                 uint32_t i);
void util_string_list_add_with_length(struct util_string_list_struct* p_list,
                                      const char* p_str,
                                      uint32_t len);

void util_string_split(struct util_string_list_struct* p_list,
                       const char* p_str,
                       char split_char);

#endif /* BEEBJIT_UTIL_STRING_H */
