#include "util_string.h"

#include "util.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

struct util_string_list_struct {
  char** p_string_list;
  uint32_t alloc_count;
  uint32_t num_strings;
};

struct util_string_list_struct*
util_string_list_alloc(void) {
  struct util_string_list_struct* p_list =
      util_mallocz(sizeof(struct util_string_list_struct));
  return p_list;
}

void
util_string_list_free(struct util_string_list_struct* p_list) {
  util_string_list_clear(p_list);
  util_free(p_list->p_string_list);
  util_free(p_list);
}

void
util_string_list_clear(struct util_string_list_struct* p_list) {
  uint32_t i;

  for (i = 0; i < p_list->num_strings; ++i) {
    util_free(p_list->p_string_list[i]);
  }
  p_list->num_strings = 0;
}

uint32_t
util_string_list_get_count(struct util_string_list_struct* p_list) {
  return p_list->num_strings;
}

static void
util_string_list_expand(struct util_string_list_struct* p_list) {
  uint32_t new_count;
  uint32_t alloc_size;

  if (p_list->alloc_count == 0) {
    new_count = 4;
  } else {
    new_count = (p_list->alloc_count * 2);
  }
  if (new_count <= p_list->alloc_count) {
    util_bail("!");
  }

  alloc_size = (new_count * sizeof(char*));
  if (alloc_size <= new_count) {
    util_bail("!");
  }
  p_list->p_string_list = util_realloc(p_list->p_string_list, alloc_size);
  p_list->alloc_count = new_count;
}

void
util_string_list_add_with_length(struct util_string_list_struct* p_list,
                                 const char* p_str,
                                 uint32_t len) {
  char* p_new_str;

  if (p_list->num_strings == p_list->alloc_count) {
    util_string_list_expand(p_list);
  }
  p_new_str = util_malloc(len + 1);
  (void) memcpy(p_new_str, p_str, len);
  p_new_str[len] = '\0';
  p_list->p_string_list[p_list->num_strings] = p_new_str;
  p_list->num_strings++;
}

char*
util_string_list_get_string(struct util_string_list_struct* p_list,
                            uint32_t i) {
  if (i >= p_list->num_strings) {
    util_bail("bad index");
  }
  return p_list->p_string_list[i];
}

void
util_string_split(struct util_string_list_struct* p_list,
                  const char* p_str,
                  char split_char) {
  uint32_t i;
  uint32_t start;
  size_t len = strlen(p_str);

  (void) split_char;

  if (len > INT_MAX) {
    util_bail("!");
  }
  /* Include the NULL terminator. */
  len++;

  util_string_list_clear(p_list);

  start = 0;
  for (i = 0; i < len; ++i) {
    uint32_t chunk_len;
    char c = p_str[i];
    if (isspace(c) || (c == '\0')) {
      /* Something to do. */
    } else {
      continue;
    }
    chunk_len = (i - start);
    if (chunk_len == 0) {
      continue;
    }

    util_string_list_add_with_length(p_list, (p_str + start), chunk_len);

    while (isspace(c)) {
      ++i;
      c = p_str[i];
    }
  }
}
