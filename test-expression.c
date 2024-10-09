/* Appends at the end of expression.c. */

#include "test.h"

static int64_t s_test_var = 0;
static uint8_t s_test_buf[8];

static int64_t
expression_test_read_one_func(void* p, uint32_t index) {
  (void) p;
  (void) index;
  return 1;
}

static int64_t
expression_test_read_two_func(void* p, uint32_t index) {
  (void) p;
  (void) index;
  return 2;
}

static int64_t
expression_test_read_index_func(void* p, uint32_t index) {
  (void) p;
  return index;
}

static int64_t
expression_test_read_var_func(void* p, uint32_t index) {
  (void) p;
  (void) index;
  return s_test_var;
}

static int64_t
expression_test_read_buf_func(void* p, uint32_t index) {
  (void) p;
  if (index < sizeof(s_test_buf)) {
    return s_test_buf[index];
  }
  return -1;
}

static expression_var_read_func_t
expression_test_var_read_lookup_func(void* p, const char* p_name) {
  (void) p;
  if (!strcmp(p_name, "one")) {
    return expression_test_read_one_func;
  }
  if (!strcmp(p_name, "two")) {
    return expression_test_read_two_func;
  }
  if (!strcmp(p_name, "getindex")) {
    return expression_test_read_index_func;
  }
  if (!strcmp(p_name, "var")) {
    return expression_test_read_var_func;
  }
  if (!strcmp(p_name, "buf")) {
    return expression_test_read_buf_func;
  }
  return NULL;
}

static void
expression_test_write_var_func(void* p, uint32_t index, int64_t value) {
  (void) p;
  (void) index;
  s_test_var = value;
}

static void
expression_test_write_buf_func(void* p, uint32_t index, int64_t value) {
  (void) p;
  if (index < sizeof(s_test_buf)) {
    s_test_buf[index] = value;
  }
}

static expression_var_write_func_t
expression_test_var_write_lookup_func(void* p, const char* p_name) {
  (void) p;
  if (!strcmp(p_name, "var")) {
    return expression_test_write_var_func;
  }
  if (!strcmp(p_name, "buf")) {
    return expression_test_write_buf_func;
  }
  return NULL;
}

static struct expression_struct*
expression_test_get_expression(void) {
  return expression_create(expression_test_var_read_lookup_func,
                           expression_test_var_write_lookup_func,
                           NULL);
}

static void
expression_test_basic(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "");
  test_expect_u32(0, expression_get_tree_size(p_expression));
  expression_parse(p_expression, " ");
  test_expect_u32(0, expression_get_tree_size(p_expression));

  expression_parse(p_expression, "7");
  test_expect_u32(1, expression_get_tree_size(p_expression));
  test_expect_u32(7, expression_execute(p_expression));

  expression_parse(p_expression, "1 + 2");
  test_expect_u32(3, expression_execute(p_expression));
  expression_parse(p_expression, "1+2");
  test_expect_u32(3, expression_execute(p_expression));

  expression_parse(p_expression, "0x11");
  test_expect_u32(17, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_left_to_right(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "1 + 2 + 3");
  test_expect_u32(6, expression_execute(p_expression));

  expression_parse(p_expression, "10 - 2 + 3");
  test_expect_u32(11, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_precedence(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "1 * 2 + 3");
  test_expect_u32(5, expression_execute(p_expression));

  expression_parse(p_expression, "1 + 2 * 3");
  test_expect_u32(7, expression_execute(p_expression));

  expression_parse(p_expression, "(1 + 2) * 3");
  test_expect_u32(9, expression_execute(p_expression));

  expression_parse(p_expression, "1 + (2 * 3)");
  test_expect_u32(7, expression_execute(p_expression));

  expression_parse(p_expression, "1 + (2 * 3) + 1");
  test_expect_u32(8, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_variables(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "one + two");
  test_expect_u32(3, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_operators(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "2 > 1");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 > 2");
  test_expect_u32(0, expression_execute(p_expression));
  expression_parse(p_expression, "2 >= 1");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 >= 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "1 < 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 < 2");
  test_expect_u32(0, expression_execute(p_expression));
  expression_parse(p_expression, "1 <= 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 <= 2");
  test_expect_u32(1, expression_execute(p_expression));

  expression_parse(p_expression, "10 / 2");
  test_expect_u32(5, expression_execute(p_expression));

  expression_parse(p_expression, "2 && 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 && 0");
  test_expect_u32(0, expression_execute(p_expression));
  expression_parse(p_expression, "0 && 2");
  test_expect_u32(0, expression_execute(p_expression));

  expression_parse(p_expression, "2 || 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "2 || 0");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "0 || 2");
  test_expect_u32(1, expression_execute(p_expression));
  expression_parse(p_expression, "0 || 0");
  test_expect_u32(0, expression_execute(p_expression));

  expression_parse(p_expression, "255 & 7");
  test_expect_u32(7, expression_execute(p_expression));
  expression_parse(p_expression, "1 | 7");
  test_expect_u32(7, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_array(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "getindex[1 + 2]");
  test_expect_u32(3, expression_execute(p_expression));

  expression_parse(p_expression, "1+(1+1)");
  test_expect_u32(3, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_assign(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "var = 42");
  test_expect_u32(42, expression_execute(p_expression));
  expression_parse(p_expression, "var");
  test_expect_u32(42, expression_execute(p_expression));

  expression_parse(p_expression, "buf[7] = 101");
  test_expect_u32(101, expression_execute(p_expression));
  expression_parse(p_expression, "buf[7]");
  test_expect_u32(101, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_misc(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "(getindex[61])");
  test_expect_u32(61, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_parens(void) {
  struct expression_struct* p_expression = expression_test_get_expression();

  expression_parse(p_expression, "((9))");
  test_expect_u32(9, expression_execute(p_expression));

  expression_parse(p_expression, "(1)+(2)*(3)");
  test_expect_u32(7, expression_execute(p_expression));

  expression_parse(p_expression, "(1)*(2)+(3)");
  test_expect_u32(5, expression_execute(p_expression));

  expression_parse(p_expression, "((1)||1)");
  test_expect_u32(1, expression_execute(p_expression));

  expression_destroy(p_expression);
}

void
expression_test() {
  expression_test_basic();
  expression_test_left_to_right();
  expression_test_precedence();
  expression_test_variables();
  expression_test_operators();
  expression_test_array();
  expression_test_assign();
  expression_test_misc();
  expression_test_parens();
}
