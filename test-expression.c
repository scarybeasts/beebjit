/* Appends at the end of expression.c. */

#include "test.h"

static int64_t
expression_test_variable_read_callback(void* p,
                                       const char* p_name,
                                       uint32_t index) {
  (void) p;
  (void) index;

  if (!strcmp(p_name, "one")) {
    return 1;
  }
  if (!strcmp(p_name, "two")) {
    return 2;
  }

  if (!strcmp(p_name, "getindex")) {
    return index;
  }

  return 0;
}

static void
expression_test_variable_write_callback(void* p,
                                        const char* p_name,
                                        uint32_t index,
                                        int64_t value) {
  (void) p;
  (void) p_name;
  (void) index;
  (void) value;
}

static struct expression_struct*
expression_test_get_expression(void) {
  return expression_create(expression_test_variable_read_callback,
                           expression_test_variable_write_callback,
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
}
