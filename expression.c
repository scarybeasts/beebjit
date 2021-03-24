#include "expression.h"

#include "log.h"
#include "util.h"
#include "util_container.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct expression_struct {
  int64_t (*p_variable_read_callback)(void*, const char*, uint32_t);
  void (*p_variable_write_callback)(void*, const char*, uint32_t, int64_t);
  void* p_variable_object;
  char* p_expr_str;
  struct util_tree_struct* p_tree;
  struct util_tree_node_struct* p_current_node;
};

enum {
  k_expression_node_integer = 1,
  k_expression_node_var = 2,
  k_expression_node_equal = 3,
  k_expression_node_not_equal = 4,
  k_expression_node_greater_than = 5,
  k_expression_node_greater_than_equal = 6,
  k_expression_node_less_than = 7,
  k_expression_node_less_than_equal = 8,
  k_expression_node_plus = 9,
  k_expression_node_minus = 10,
  k_expression_node_multiply = 11,
  k_expression_node_divide = 12,
  k_expression_node_logical_and = 13,
  k_expression_node_logical_or = 14,
  k_expression_node_bitwise_and = 15,
  k_expression_node_bitwise_or = 16,
  k_expression_node_paren_open = 17,
  k_expression_node_paren_close = 18,
  k_expression_node_square_open = 19,
  k_expression_node_square_close = 20,
  k_expression_node_assign = 21,
};

struct expression_struct*
expression_create(int64_t (*p_variable_read_callback)(void* p,
                                                      const char* p_name,
                                                      uint32_t index),
                  void (*p_variable_write_callback)(void* p,
                                                    const char* p_name,
                                                    uint32_t index,
                                                    int64_t value),
                  void* p_variable_object) {
  struct expression_struct* p_expression =
      util_mallocz(sizeof(struct expression_struct));
  p_expression->p_variable_read_callback = p_variable_read_callback;
  p_expression->p_variable_write_callback = p_variable_write_callback;
  p_expression->p_variable_object = p_variable_object;
  p_expression->p_expr_str = NULL;
  p_expression->p_tree = util_tree_alloc();
  return p_expression;
}

void
expression_destroy(struct expression_struct* p_expression) {
  util_free(p_expression->p_expr_str);
  util_tree_free(p_expression->p_tree);
  util_free(p_expression);
}

static void
expression_clear(struct expression_struct* p_expression) {
  util_free(p_expression->p_expr_str);
  p_expression->p_expr_str = NULL;
  util_tree_free(p_expression->p_tree);
  p_expression->p_tree = util_tree_alloc();
  p_expression->p_current_node = NULL;
}

static int32_t
expression_get_precedence(int32_t type) {
  /* These precedences follow the C rules. */
  int32_t ret = 0;
  switch (type) {
  case k_expression_node_integer:
  case k_expression_node_var:
    ret = 100;
    break;
  case k_expression_node_multiply:
  case k_expression_node_divide:
    ret = 75;
    break;
  case k_expression_node_plus:
  case k_expression_node_minus:
    ret = 70;
    break;
  case k_expression_node_greater_than:
  case k_expression_node_greater_than_equal:
  case k_expression_node_less_than:
  case k_expression_node_less_than_equal:
    ret = 55;
    break;
  case k_expression_node_equal:
  case k_expression_node_not_equal:
    ret = 50;
    break;
  case k_expression_node_bitwise_and:
    ret = 45;
    break;
  case k_expression_node_bitwise_or:
    ret = 40;
    break;
  case k_expression_node_logical_and:
    ret = 35;
    break;
  case k_expression_node_logical_or:
    ret = 30;
    break;
  case k_expression_node_assign:
    ret = 10;
    break;
  case k_expression_node_paren_open:
  case k_expression_node_paren_close:
  case k_expression_node_square_open:
  case k_expression_node_square_close:
    ret = 0;
    break;
  default:
    ret = 0;
    break;
  }
  return ret;
}

static void
expression_parent_walk_for_closure(struct expression_struct* p_expression,
                                   int32_t search_type,
                                   const char* p_closing_symbol) {
  struct util_tree_node_struct* p_parent_node = p_expression->p_current_node;
  while (1) {
    int parent_type;
    p_parent_node = util_tree_node_get_parent(p_parent_node);
    if (p_parent_node == NULL) {
      break;
    }
    parent_type = util_tree_node_get_type(p_parent_node);
    if (parent_type == search_type) {
      break;
    }
  }
  if (p_parent_node == NULL) {
    log_do_log(k_log_misc, k_log_warning, "mismatched %s", p_closing_symbol);
  } else {
    p_parent_node = util_tree_node_get_parent(p_parent_node);
  }
  p_expression->p_current_node = p_parent_node;
}

static void
expression_process_token(struct expression_struct* p_expression,
                         const char* p_token_str) {
  int32_t new_precedence;
  struct util_tree_struct* p_tree = p_expression->p_tree;
  struct util_tree_node_struct* p_new_node = NULL;
  struct util_tree_node_struct* p_parent_node = p_expression->p_current_node;
  char c = p_token_str[0];
  int32_t type = 0;
  assert(!isspace(c));
  if (isdigit(c)) {
    int64_t val;
    int base = 10;
    const char* p_number_str = p_token_str;
    if (!strncmp(p_number_str, "0x", 2)) {
      p_number_str += 2;
      base = 16;
    }
    val = strtoll(p_number_str, NULL, base);
    type = k_expression_node_integer;
    p_new_node = util_tree_node_alloc(k_expression_node_integer);
    util_tree_node_set_int_value(p_new_node, val);
  } else if (isalpha(c)) {
    type = k_expression_node_var;
    p_new_node = util_tree_node_alloc(k_expression_node_var);
    util_tree_node_set_object_value(p_new_node, util_strdup(p_token_str));
  } else {
    int do_node_alloc = 1;
    if (!strcmp(p_token_str, "==")) {
      type = k_expression_node_equal;
    } else if (!strcmp(p_token_str, "!=")) {
      type = k_expression_node_not_equal;
    } else if (!strcmp(p_token_str, ">")) {
      type = k_expression_node_greater_than;
    } else if (!strcmp(p_token_str, ">=")) {
      type = k_expression_node_greater_than_equal;
    } else if (!strcmp(p_token_str, "<")) {
      type = k_expression_node_less_than;
    } else if (!strcmp(p_token_str, "<=")) {
      type = k_expression_node_less_than_equal;
    } else if (!strcmp(p_token_str, "+")) {
      type = k_expression_node_plus;
    } else if (!strcmp(p_token_str, "-")) {
      type = k_expression_node_minus;
    } else if (!strcmp(p_token_str, "*")) {
      type = k_expression_node_multiply;
    } else if (!strcmp(p_token_str, "/")) {
      type = k_expression_node_divide;
    } else if (!strcmp(p_token_str, "&&")) {
      type = k_expression_node_logical_and;
    } else if (!strcmp(p_token_str, "||")) {
      type = k_expression_node_logical_or;
    } else if (!strcmp(p_token_str, "&")) {
      type = k_expression_node_bitwise_and;
    } else if (!strcmp(p_token_str, "|")) {
      type = k_expression_node_bitwise_or;
    } else if (!strcmp(p_token_str, "(")) {
      type = k_expression_node_paren_open;
    } else if (!strcmp(p_token_str, ")")) {
      type = k_expression_node_paren_close;
      do_node_alloc = 0;
    } else if (!strcmp(p_token_str, "[")) {
      type = k_expression_node_square_open;
    } else if (!strcmp(p_token_str, "]")) {
      type = k_expression_node_square_close;
      do_node_alloc = 0;
    } else if (!strcmp(p_token_str, "=")) {
      type = k_expression_node_assign;
    }
    if (type == 0) {
      log_do_log(k_log_misc, k_log_warning, "unknown operator %s", p_token_str);
    }
    if (do_node_alloc) {
      p_new_node = util_tree_node_alloc(type);
    }
  }

  /* Closure of matched nodes just walks back up the tree. */
  if (type == k_expression_node_paren_close) {
    expression_parent_walk_for_closure(p_expression,
                                       k_expression_node_paren_open,
                                       ")");
    return;
  } else if (type == k_expression_node_square_close) {
    expression_parent_walk_for_closure(p_expression,
                                       k_expression_node_square_open,
                                       "]");
    return;
  }

  if ((type == k_expression_node_paren_open) ||
      (type == k_expression_node_square_open)) {
    /* ( and [ are special. They stop tree walks (precedence 0 for tree walks)
     * but must also go exactly in our current position in the tree.
     */
    new_precedence = INT_MAX;
  } else {
    new_precedence = expression_get_precedence(type);
  }
  while (1) {
    int32_t curr_type;
    int32_t curr_precedence;
    if (p_parent_node == NULL) {
      break;
    }
    curr_type = util_tree_node_get_type(p_parent_node);
    curr_precedence = expression_get_precedence(curr_type);
    if (new_precedence > curr_precedence) {
      break;
    }
    p_parent_node = util_tree_node_get_parent(p_parent_node);
  }
  if (p_parent_node == NULL) {
    struct util_tree_node_struct* p_old_root = util_tree_get_root(p_tree);
    util_tree_set_root(p_tree, p_new_node);
    if (p_old_root != NULL) {
      util_tree_node_add_child(p_new_node, p_old_root);
    }
  } else {
    struct util_tree_node_struct* p_reparent_node = NULL;
    if (p_parent_node != p_expression->p_current_node) {
      uint32_t index = util_tree_node_get_num_children(p_parent_node);
      index--;
      p_reparent_node = util_tree_node_remove_child(p_parent_node, index);
    }
    util_tree_node_add_child(p_parent_node, p_new_node);
    if (p_reparent_node != NULL) {
      util_tree_node_add_child(p_new_node, p_reparent_node);
    }
  }

  p_expression->p_current_node = p_new_node;
}

int64_t
expression_parse(struct expression_struct* p_expression,
                 const char* p_expr_str) {
  uint32_t i;
  uint32_t len;
  size_t full_len = strlen(p_expr_str);

  (void) p_expression;
  (void) p_expr_str;

  if (full_len >= INT_MAX) {
    util_bail("!");
  }
  len = full_len;

  expression_clear(p_expression);

  p_expression->p_expr_str = util_strdup(p_expr_str);

  i = 0;
  while (i < len) {
    char token_buf[256];
    uint32_t token_len;
    char c;
    uint32_t max_token_len = (sizeof(token_buf) - 1);
    while (isspace(p_expr_str[i])) {
      i++;
    }
    token_len = 0;
    while (isalpha(c = p_expr_str[i]) && (token_len < max_token_len)) {
      token_buf[token_len++] = c;
      i++;
    }
    if (token_len > 0) {
      token_buf[token_len] = '\0';
      expression_process_token(p_expression, &token_buf[0]);
      token_len = 0;
    }
    c = p_expr_str[i];
    if (isdigit(c)) {
      while ((isdigit(c = p_expr_str[i]) || isalpha(c)) &&
             (token_len < max_token_len)) {
        token_buf[token_len++] = c;
        i++;
      }
    }
    if (token_len > 0) {
      token_buf[token_len] = '\0';
      expression_process_token(p_expression, &token_buf[0]);
      token_len = 0;
    }
    while ((c = p_expr_str[i]) != '\0' &&
           !isspace(c) &&
           !isalpha(c) &&
           !isdigit(c) &&
           (token_len < max_token_len)) {
      token_buf[token_len++] = c;
      i++;
    }
    if (token_len > 0) {
      token_buf[token_len] = '\0';
      expression_process_token(p_expression, &token_buf[0]);
      token_len = 0;
    }
  }

  return 0;
}

static int64_t
expression_execute_node(struct expression_struct* p_expression,
                        struct util_tree_node_struct* p_node) {
  int64_t ret = 0;
  int32_t type = util_tree_node_get_type(p_node);
  uint32_t num_children = util_tree_node_get_num_children(p_node);
  struct util_tree_node_struct* p_child_node_1 = NULL;
  struct util_tree_node_struct* p_child_node_2 = NULL;
  int64_t lhs;
  int64_t rhs;
  uint32_t index;

  if (num_children > 0) {
    p_child_node_1 = util_tree_node_get_child(p_node, 0);
  }
  if (num_children > 1) {
    p_child_node_2 = util_tree_node_get_child(p_node, 1);
  }

  switch (type) {
  case k_expression_node_integer:
    ret = util_tree_node_get_int_value(p_node);
    break;
  case k_expression_node_var:
    index = 0;
    if (num_children == 1) {
      index = expression_execute_node(p_expression, p_child_node_1);
    }
    ret = p_expression->p_variable_read_callback(
        p_expression->p_variable_object,
        (const char*) util_tree_node_get_object_value(p_node),
        index);
    break;
  case k_expression_node_plus:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret += expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_minus:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret -= expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_multiply:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret *= expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_divide:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret /= expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_equal:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs == rhs);
    }
    break;
  case k_expression_node_not_equal:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs != rhs);
    }
    break;
  case k_expression_node_less_than:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs < rhs);
    }
    break;
  case k_expression_node_less_than_equal:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs <= rhs);
    }
    break;
  case k_expression_node_greater_than:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs > rhs);
    }
    break;
  case k_expression_node_greater_than_equal:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = (lhs >= rhs);
    }
    break;
  case k_expression_node_logical_and:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = 0;
      if (lhs) {
        rhs = expression_execute_node(p_expression, p_child_node_2);
      }
      ret = (lhs && rhs);
    }
    break;
  case k_expression_node_logical_or:
    if (num_children == 2) {
      lhs = expression_execute_node(p_expression, p_child_node_1);
      rhs = 0;
      if (!lhs) {
        rhs = expression_execute_node(p_expression, p_child_node_2);
      }
      ret = (lhs || rhs);
    }
    break;
  case k_expression_node_bitwise_and:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret &= expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_bitwise_or:
    if (num_children == 2) {
      ret = expression_execute_node(p_expression, p_child_node_1);
      ret |= expression_execute_node(p_expression, p_child_node_2);
    }
    break;
  case k_expression_node_paren_open:
  case k_expression_node_square_open:
    if (num_children == 1) {
      ret = expression_execute_node(p_expression, p_child_node_1);
    }
    break;
  case k_expression_node_assign:
    if (num_children == 2) {
      rhs = expression_execute_node(p_expression, p_child_node_2);
      ret = rhs;
      if (util_tree_node_get_type(p_child_node_1) == k_expression_node_var) {
        index = 0;
        if (util_tree_node_get_num_children(p_child_node_1) == 1) {
          struct util_tree_node_struct* p_index_node =
              util_tree_node_get_child(p_child_node_1, 0);
          index = expression_execute_node(p_expression, p_index_node);
        }
        p_expression->p_variable_write_callback(
            p_expression->p_variable_object,
            (const char*) util_tree_node_get_object_value(p_child_node_1),
            index,
            rhs);
      }
    }
    break;
  default:
    assert(0);
  }

  return ret;
}

const char*
expression_get_original_string(struct expression_struct* p_expression) {
  return p_expression->p_expr_str;
}

uint32_t
expression_get_tree_size(struct expression_struct* p_expression) {
  return util_tree_get_tree_size(p_expression->p_tree);
}

int64_t
expression_execute(struct expression_struct* p_expression) {
  struct util_tree_node_struct* p_node;
  if (p_expression->p_tree == NULL) {
    return 0;
  }
  p_node = util_tree_get_root(p_expression->p_tree);
  if (p_node == NULL) {
    return 0;
  }
  return expression_execute_node(p_expression, p_node);
}

#include "test-expression.c"
