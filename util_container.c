#include "util_container.h"

#include "util.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

struct util_list_struct {
  intptr_t* p_list;
  uint32_t num_alloc;
  uint32_t num_used;
};

struct util_tree_node_struct {
  int32_t type;
  int64_t value;
  void* p_value;
  struct util_tree_node_struct* p_parent;
  struct util_list_struct child_list;
};

struct util_tree_struct {
  struct util_tree_node_struct* p_root;
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

static void
util_tree_node_free(struct util_tree_node_struct* p_node) {
  uint32_t i;
  uint32_t num_children = p_node->child_list.num_used;
  for (i = 0; i < num_children; ++i) {
    struct util_tree_node_struct* p_child_node =
        (struct util_tree_node_struct*) util_list_get(&p_node->child_list, i);
    assert(p_child_node->p_parent == p_node);
    util_tree_node_free(p_child_node);
  }
  util_free(p_node->p_value);
  util_free(p_node);
}

static uint32_t
util_tree_node_size(struct util_tree_node_struct* p_node, uint32_t size) {
  uint32_t i;
  uint32_t num_children = p_node->child_list.num_used;
  for (i = 0; i < num_children; ++i) {
    struct util_tree_node_struct* p_child_node =
        (struct util_tree_node_struct*) util_list_get(&p_node->child_list, i);
    size = util_tree_node_size(p_child_node, size);
  }
  return (size + 1);
}

struct util_tree_struct*
util_tree_alloc(void) {
  struct util_tree_struct* p_tree =
      util_mallocz(sizeof(struct util_tree_struct));
  p_tree->p_root = NULL;
  return p_tree;
}

void util_tree_free(struct util_tree_struct* p_tree) {
  if (p_tree->p_root != NULL) {
    util_tree_node_free(p_tree->p_root);
  }
  util_free(p_tree);
}

uint32_t
util_tree_get_tree_size(struct util_tree_struct* p_tree) {
  uint32_t size = 0;
  if (p_tree->p_root != NULL) {
    size = util_tree_node_size(p_tree->p_root, size);
  }
  return size;
}

struct util_tree_node_struct*
util_tree_get_root(struct util_tree_struct* p_tree) {
  return p_tree->p_root;
}

void
util_tree_set_root(struct util_tree_struct* p_tree,
                   struct util_tree_node_struct* p_node) {
  if (p_node->p_parent != NULL) {
    util_bail("root node has parent");
  }
  p_tree->p_root = p_node;
}

struct util_tree_node_struct*
util_tree_node_alloc(int32_t type) {
  struct util_tree_node_struct* p_node =
      util_mallocz(sizeof(struct util_tree_node_struct));
  p_node->type = type;
  p_node->p_parent = NULL;
  p_node->p_value = NULL;
  return p_node;
}

int32_t
util_tree_node_get_type(struct util_tree_node_struct* p_node) {
  return p_node->type;
}

void
util_tree_node_set_type(struct util_tree_node_struct* p_node, int32_t type) {
  p_node->type = type;
}

struct util_tree_node_struct*
util_tree_node_get_parent(struct util_tree_node_struct* p_node) {
  return p_node->p_parent;
}

uint32_t
util_tree_node_get_num_children(struct util_tree_node_struct* p_node) {
  return p_node->child_list.num_used;
}

struct util_tree_node_struct*
util_tree_node_get_child(struct util_tree_node_struct* p_node, uint32_t index) {
  if (index >= p_node->child_list.num_used) {
    util_bail("bad index");
  }
  return (struct util_tree_node_struct*) p_node->child_list.p_list[index];
}

void
util_tree_node_add_child(struct util_tree_node_struct* p_node,
                         struct util_tree_node_struct* p_child_node) {
  if (p_child_node->p_parent != NULL) {
    util_bail("node has parent");
  }
  p_child_node->p_parent = p_node;
  util_list_add(&p_node->child_list, (intptr_t) p_child_node);
}

struct util_tree_node_struct*
util_tree_node_remove_child(struct util_tree_node_struct* p_node,
                            uint32_t index) {
  struct util_tree_node_struct* p_removed_node;
  if (index >= p_node->child_list.num_used) {
    util_bail("bad index");
  }
  p_removed_node = (struct util_tree_node_struct*) util_list_remove(
      &p_node->child_list, index);
  p_removed_node->p_parent = NULL;

  return p_removed_node;
}

int64_t
util_tree_node_get_int_value(struct util_tree_node_struct* p_node) {
  return p_node->value;
}

void
util_tree_node_set_int_value(struct util_tree_node_struct* p_node,
                             int64_t val) {
  p_node->value = val;
}

void*
util_tree_node_get_object_value(struct util_tree_node_struct* p_node) {
  return p_node->p_value;
}

void
util_tree_node_set_object_value(struct util_tree_node_struct* p_node,
                                void* p_object) {
  util_free(p_node->p_value);
  p_node->p_value = p_object;
}
