#ifndef BEEBJIT_UTIL_CONTAINER_H
#define BEEBJIT_UTIL_CONTAINER_H

#include <stdint.h>

struct util_list_struct;
struct util_tree_struct;

/* List. */
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


/* Tree. */
struct util_tree_struct* util_tree_alloc(void);
void util_tree_free(struct util_tree_struct* p_tree);
uint32_t util_tree_get_tree_size(struct util_tree_struct* p_tree);
struct util_tree_node_struct* util_tree_get_root(
    struct util_tree_struct* p_tree);
void util_tree_set_root(struct util_tree_struct* p_tree,
                        struct util_tree_node_struct* p_node);

struct util_tree_node_struct* util_tree_node_alloc(int32_t type);
int32_t util_tree_node_get_type(struct util_tree_node_struct* p_node);
void util_tree_node_set_type(struct util_tree_node_struct* p_node,
                             int32_t type);
struct util_tree_node_struct* util_tree_node_get_parent(
    struct util_tree_node_struct* p_node);
uint32_t util_tree_node_get_num_children(struct util_tree_node_struct* p_node);
struct util_tree_node_struct* util_tree_node_get_child(
    struct util_tree_node_struct* p_node,
    uint32_t index);
void util_tree_node_add_child(struct util_tree_node_struct* p_node,
                              struct util_tree_node_struct* p_child_node);
struct util_tree_node_struct* util_tree_node_remove_child(
    struct util_tree_node_struct* p_node, uint32_t index);
int64_t util_tree_node_get_int_value(struct util_tree_node_struct* p_node);
void util_tree_node_set_int_value(struct util_tree_node_struct* p_node,
                                  int64_t val);
void* util_tree_node_get_object_value(struct util_tree_node_struct* p_node);
void util_tree_node_set_object_value(struct util_tree_node_struct* p_node,
                                     void* p_object);

#endif /* BEEBJIT_UTIL_CONTAINER_H */
