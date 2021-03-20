#include "util_container.h"

#include "util.h"

#include <limits.h>
#include <string.h>

struct util_list_struct {
  intptr_t* p_list;
  uint32_t num_alloc;
  uint32_t num_used;
};

struct util_list_struct*
util_list_alloc(void) {
  struct util_list_struct* p_list =
      util_mallocz(sizeof(struct util_list_struct));
  return p_list;
}

void
util_list_free(struct util_list_struct* p_list) {
  util_list_clear(p_list);
  util_free(p_list->p_list);
  util_free(p_list);
}

void
util_list_clear(struct util_list_struct* p_list) {
  p_list->num_used = 0;
}

uint32_t
util_list_get_count(struct util_list_struct* p_list) {
  return p_list->num_used;
}

intptr_t
util_list_get(struct util_list_struct* p_list, uint32_t index) {
  if (index >= p_list->num_used) {
    util_bail("bad index");
  }
  return p_list->p_list[index];
}

static void
util_list_expand(struct util_list_struct* p_list) {
  uint32_t new_num_alloc;
  uint32_t alloc_size;

  if (p_list->num_alloc == 0) {
    new_num_alloc = 4;
  } else {
    new_num_alloc = (p_list->num_alloc * 2);
  }
  if (new_num_alloc <= p_list->num_alloc) {
    util_bail("!");
  }

  alloc_size = (new_num_alloc * sizeof(intptr_t));
  if (alloc_size <= new_num_alloc) {
    util_bail("!");
  }
  p_list->p_list = util_realloc(p_list->p_list, alloc_size);
  p_list->num_alloc = new_num_alloc;
}

intptr_t
util_list_set(struct util_list_struct* p_list, uint32_t index, intptr_t item) {
  intptr_t old_item;
  if (index >= p_list->num_alloc) {
    util_bail("bad index");
  }
  old_item = p_list->p_list[index];
  p_list->p_list[index] = item;

  return old_item;
}

void
util_list_add(struct util_list_struct* p_list, intptr_t item) {
  if (p_list->num_used == p_list->num_alloc) {
    util_list_expand(p_list);
  }
  p_list->p_list[p_list->num_used] = item;
  p_list->num_used++;
}

void
util_list_insert(struct util_list_struct* p_list,
                 uint32_t index,
                 intptr_t item) {
  if (index > p_list->num_used) {
    util_bail("bad index");
  }
  if (p_list->num_used == p_list->num_alloc) {
    util_list_expand(p_list);
  }
  (void) memmove(&p_list->p_list[index + 1],
                 &p_list->p_list[index],
                 ((p_list->num_used - index) * sizeof(intptr_t)));
  p_list->p_list[index] = item;
  p_list->num_used++;
}

void util_list_append_list(struct util_list_struct* p_list,
                           struct util_list_struct* p_src_list) {
  uint32_t i;

  for (i = 0; i < p_src_list->num_used; ++i) {
    util_list_add(p_list, p_src_list->p_list[i]);
  }
}

intptr_t
util_list_remove(struct util_list_struct* p_list, uint32_t index) {
  intptr_t item;
  if (index >= p_list->num_used) {
    util_bail("bad index");
  }
  item = util_list_get(p_list, index);
  (void) memmove(&p_list->p_list[index],
                 &p_list->p_list[index + 1],
                 ((p_list->num_used - index - 1) * sizeof(intptr_t)));
  p_list->num_used--;

  return item;
}
