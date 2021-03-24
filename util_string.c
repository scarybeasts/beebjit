#include "util_string.h"

#include "util.h"
#include "util_container.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

struct util_string_list_struct*
util_string_list_alloc(void) {
  return (struct util_string_list_struct*) util_list_alloc();
}

void
util_string_list_free(struct util_string_list_struct* p_string_list) {
  util_string_list_clear(p_string_list);
  util_list_free((struct util_list_struct*) p_string_list);
}

void
util_string_list_clear(struct util_string_list_struct* p_string_list) {
  uint32_t i;
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;
  uint32_t num_strings = util_list_get_count(p_list);

  for (i = 0; i < num_strings; ++i) {
    char* p_str = (char*) util_list_get(p_list, i);
    util_free(p_str);
  }
  util_list_clear(p_list);
}

uint32_t
util_string_list_get_count(struct util_string_list_struct* p_string_list) {
  return util_list_get_count((struct util_list_struct*) p_string_list);
}

const char*
util_string_list_get_string(struct util_string_list_struct* p_string_list,
                            uint32_t index) {
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;
  return (const char*) util_list_get(p_list, index);
}

void
util_string_list_set_at_with_length(
    struct util_string_list_struct* p_string_list,
    uint32_t index,
    const char* p_str,
    uint32_t len) {
  char* p_new_str;
  char* p_old_str;
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;

  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }
  p_new_str = util_malloc(len + 1);
  (void) memcpy(p_new_str, p_str, len);
  p_new_str[len] = '\0';

  p_old_str = (char*) util_list_set(p_list, index, (intptr_t) p_new_str);
  util_free(p_old_str);
}

void
util_string_list_set_at(struct util_string_list_struct* p_string_list,
                        uint32_t index,
                        const char* p_str) {
  size_t len = strlen(p_str);
  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }

  util_string_list_set_at_with_length(p_string_list, index, p_str, len);
}

void
util_string_list_add_with_length(struct util_string_list_struct* p_string_list,
                                 const char* p_str,
                                 uint32_t len) {
  uint32_t index;
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;
  index = util_list_get_count(p_list);
  util_list_add(p_list, 0);
  util_string_list_set_at_with_length(p_string_list, index, p_str, len);
}

void
util_string_list_add(struct util_string_list_struct* p_string_list,
                     const char* p_str) {
  size_t len = strlen(p_str);
  if (len >= (INT_MAX - 1)) {
    util_bail("!");
  }
  util_string_list_add_with_length(p_string_list, p_str, len);
}

void
util_string_list_insert(struct util_string_list_struct* p_string_list,
                        uint32_t index,
                        const char* p_str) {
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;
  util_list_insert(p_list, index, 0);
  util_string_list_set_at(p_string_list, index, p_str);
}

void util_string_list_append_list(
    struct util_string_list_struct* p_string_list,
    struct util_string_list_struct* p_src_string_list) {
  uint32_t i;
  struct util_list_struct* p_src_list =
      (struct util_list_struct*) p_src_string_list;
  uint32_t num_strings = util_list_get_count(p_src_list);

  for (i = 0; i < num_strings; ++i) {
    const char* p_str = (const char*) util_list_get(p_src_list, i);
    util_string_list_add(p_string_list, p_str);
  }
}

void
util_string_list_remove(struct util_string_list_struct* p_string_list,
                        uint32_t index) {
  struct util_list_struct* p_list = (struct util_list_struct*) p_string_list;
  char* p_old_str = (char*) util_list_remove(p_list, index);
  util_free(p_old_str);
}

void
util_string_split(struct util_string_list_struct* p_string_list,
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

  util_string_list_clear(p_string_list);

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
    util_string_list_add_with_length(p_string_list,
                                     (p_str + start),
                                     chunk_len);

    is_in_quote = 0;
    start = (i + 1);
  }
}
