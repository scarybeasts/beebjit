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

const char*
util_string_list_get_string(struct util_string_list_struct* p_list,
                            uint32_t i) {
  if (i >= p_list->num_strings) {
    util_bail("bad index");
  }
  return p_list->p_string_list[i];
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
util_string_list_set_at_with_length(struct util_string_list_struct* p_list,
                                    uint32_t index,
                                    const char* p_str,
                                    uint32_t len) {
  char* p_new_str;

  if (index >= p_list->num_strings) {
    util_bail("bad index");
  }
  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }
  p_new_str = util_malloc(len + 1);
  (void) memcpy(p_new_str, p_str, len);
  p_new_str[len] = '\0';
  util_free(p_list->p_string_list[index]);
  p_list->p_string_list[index] = p_new_str;
}

void
util_string_list_set_at(struct util_string_list_struct* p_list,
                        uint32_t index,
                        const char* p_str) {
  size_t len = strlen(p_str);
  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }
  util_string_list_set_at_with_length(p_list, index, p_str, len);
}

void
util_string_list_add_with_length(struct util_string_list_struct* p_list,
                                 const char* p_str,
                                 uint32_t len) {
  uint32_t index = p_list->num_strings;

  if (p_list->num_strings == p_list->alloc_count) {
    util_string_list_expand(p_list);
  }
  p_list->p_string_list[index] = NULL;
  p_list->num_strings++;
  util_string_list_set_at_with_length(p_list, index, p_str, len);
}

void
util_string_list_add(struct util_string_list_struct* p_list,
                     const char* p_str) {
  size_t len = strlen(p_str);
  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }
  util_string_list_add_with_length(p_list, p_str, len);
}

void
util_string_list_insert(struct util_string_list_struct* p_list,
                        uint32_t index,
                        const char* p_str) {
  if (index > p_list->num_strings) {
    util_bail("bad index");
  }
  if (p_list->num_strings == p_list->alloc_count) {
    util_string_list_expand(p_list);
  }
  (void) memmove(&p_list->p_string_list[index + 1],
                 &p_list->p_string_list[index],
                 ((p_list->num_strings - index) * sizeof(char*)));
  p_list->p_string_list[index] = NULL;
  p_list->num_strings++;
  util_string_list_set_at(p_list, index, p_str);
}

void util_string_list_append_list(struct util_string_list_struct* p_list,
                                  struct util_string_list_struct* p_src_list) {
  uint32_t i;

  for (i = 0; i < p_src_list->num_strings; ++i) {
    util_string_list_add(p_list, p_src_list->p_string_list[i]);
  }
}

void
util_string_list_remove(struct util_string_list_struct* p_list,
                        uint32_t index) {
  if (index >= p_list->num_strings) {
    util_bail("bad index");
  }
  util_free(p_list->p_string_list[index]);
  (void) memmove(&p_list->p_string_list[index],
                 &p_list->p_string_list[index + 1],
                 ((p_list->num_strings - index - 1) * sizeof(char*)));
  p_list->num_strings--;
}

void
util_string_split(struct util_string_list_struct* p_list,
                  const char* p_str,
                  char split_char,
                  char quote_char) {
  uint32_t i;
  uint32_t start;
  int is_in_quote;
  size_t len = strlen(p_str);

  if (len > INT_MAX) {
    util_bail("!");
  }
  /* Include the NULL terminator. */
  len++;

  util_string_list_clear(p_list);

  is_in_quote = 0;
  start = 0;
  for (i = 0; i < len; ++i) {
    uint32_t chunk_len;
    char c = p_str[i];
    if (c == '\0') {
      /* Fall through -- chunk and overall string ended. */
    } else if (c == quote_char) {
      is_in_quote = !is_in_quote;
      continue;
    } else if (c == split_char) {
      if (is_in_quote) {
        continue;
      }
      /* Fall through -- chunk ended. */
    } else {
      continue;
    }

    chunk_len = (i - start);
    /* Strip surrounding quotes. */
    if ((chunk_len >= 2) &&
        (p_str[start] == quote_char) &&
        (p_str[i - 1] == quote_char)) {
      start++;
      chunk_len -= 2;
    }
    util_string_list_add_with_length(p_list, (p_str + start), chunk_len);

    is_in_quote = 0;
    start = (i + 1);
  }
}
